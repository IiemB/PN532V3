#include "web/WebApp.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <algorithm>

#include "config.h"

namespace {

String toJson(JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  return out;
}

String hexString(const std::vector<uint8_t>& data) {
  static const char* kHex = "0123456789ABCDEF";
  String out;
  out.reserve(data.size() * 2);
  for (const uint8_t byte : data) {
    out += kHex[byte >> 4];
    out += kHex[byte & 0x0F];
  }
  return out;
}

String logJson(const String& message) {
  JsonDocument doc;
  doc["event"] = "log";
  doc["message"] = message;
  return toJson(doc);
}

void fillTag(JsonObject tag, const TagSnapshot& snapshot) {
  tag["uid"] = snapshot.uid.hex(" ");
  tag["uidCompact"] = snapshot.uid.hex();
  tag["uidLength"] = snapshot.uid.length;
  char atqa[5];
  snprintf(atqa, sizeof(atqa), "%04X", snapshot.uid.atqa);
  tag["atqa"] = atqa;
  char sak[3];
  snprintf(sak, sizeof(sak), "%02X", snapshot.uid.sak);
  tag["sak"] = sak;
  tag["kind"] = snapshot.kindName();
  tag["model"] = snapshot.model;
  tag["capacity"] = snapshot.capacity;
  tag["totalBytes"] = snapshot.totalBytes;
  tag["complete"] = snapshot.complete;
  tag["error"] = snapshot.error;
  tag["ndef"] = snapshot.ndefFound;
  tag["chunkSize"] = snapshot.kind == TagKind::MifareClassic ? 16 : 4;
  tag["memory"] = hexString(snapshot.memory);
  tag["readAgeMs"] =
      snapshot.readAtMs == 0 ? 0 : millis() - snapshot.readAtMs;

  JsonArray records = tag["records"].to<JsonArray>();
  for (const NdefRecord& record : snapshot.records) {
    JsonObject entry = records.add<JsonObject>();
    entry["type"] = record.type;
    entry["value"] = record.value;
    if (record.type == "text") {
      entry["language"] = record.language;
      entry["utf16"] = record.utf16;
    } else if (record.type == "other") {
      entry["tnf"] = record.tnf;
    }
  }
}

struct NetworkEntry {
  String ssid;
  int32_t rssi;
  int32_t encryption;
};

}  // namespace

void WebApp::begin() {
#if defined(ESP8266)
  const bool mounted = LittleFS.begin();
#else
  const bool mounted = LittleFS.begin(true);
#endif
  if (!mounted) {
    logLine("[web] LittleFS mount failed");
  }

  _tags.setSnapshotCallback([this](const TagSnapshot& snapshot) {
    JsonDocument doc;
    doc["event"] = "tag";
    doc["present"] = true;
    fillTag(doc["tag"].to<JsonObject>(), snapshot);
    broadcastEvent(toJson(doc));
  });

  _tags.setRemovedCallback([this](const TagUid& uid) {
    JsonDocument doc;
    doc["event"] = "removed";
    doc["uid"] = uid.hex(" ");
    broadcastEvent(toJson(doc));
  });

  _tags.setLogCallback([this](const String& message) { logLine(message); });

  _tags.setResultCallback([this](const WriteResult& result) {
    broadcastEvent(resultJson(result));
  });

  setupRoutes();
  _ws.begin();
  _ws.onEvent(
      [this](uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
        onWebSocketEvent(client, type, payload, length);
      });
  _server.begin();

  logLine(String("[web] http://") + _net.ip() + "/");
}

void WebApp::loop() {
  _server.handleClient();
  _ws.loop();

  if (_restartAtMs != 0 && millis() >= _restartAtMs) {
    delay(100);
    ESP.restart();
  }
}

void WebApp::setupRoutes() {
  _server.serveStatic("/", LittleFS, "/index.html");
  _server.serveStatic("/index.html", LittleFS, "/index.html");
  _server.serveStatic("/app.js", LittleFS, "/app.js");
  _server.serveStatic("/style.css", LittleFS, "/style.css");

  _server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  _server.on("/api/tag", HTTP_GET, [this]() { handleTag(); });
  _server.on("/api/history", HTTP_GET, [this]() { handleHistory(); });
  _server.on("/api/source", HTTP_POST, [this]() { handleSource(); });
  _server.on("/api/read", HTTP_POST, [this]() { handleRead(); });
  _server.on("/api/result", HTTP_GET, [this]() { handleResult(); });
  _server.on("/api/write", HTTP_POST, [this]() { handleWrite(); });
  _server.on("/api/clone", HTTP_POST, [this]() { handleClone(); });
  _server.on("/api/wifi", HTTP_POST, [this]() { handleWifiSave(); });
  _server.on("/api/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
  _server.onNotFound([this]() { handleNotFound(); });
}

void WebApp::onWebSocketEvent(uint8_t client, WStype_t type, uint8_t* payload,
                              size_t length) {
  if (type == WStype_CONNECTED) {
    String status = statusJson();
    _ws.sendTXT(client, status);
    String tag = tagJson();
    _ws.sendTXT(client, tag);
    for (const String& line : _logs) {
      String entry = logJson(line);
      _ws.sendTXT(client, entry);
    }
    return;
  }
  if (type == WStype_TEXT) {
    String message;
    message.reserve(length);
    for (size_t i = 0; i < length; ++i) {
      message += static_cast<char>(payload[i]);
    }
    if (message == "ping") {
      _ws.sendTXT(client, "{\"event\":\"pong\"}");
    }
  }
}

void WebApp::handleStatus() { sendJson(200, statusJson()); }

void WebApp::handleTag() { sendJson(200, tagJson()); }

void WebApp::handleHistory() { sendJson(200, historyJson()); }

void WebApp::handleSource() {
  const String body = _server.arg("plain");
  JsonDocument request;
  if (body.length() > 0) {
    const DeserializationError error = deserializeJson(request, body);
    if (error) {
      sendJson(400, errorJson("ERR_JSON", error.c_str()));
      return;
    }
  }

  const String uid = request["uid"] | "";
  String error;
  const bool ok = uid.length() > 0 ? _tags.setSourceFromUid(uid, error)
                                   : _tags.setSourceCurrent(error);

  JsonDocument response;
  response["ok"] = ok;
  if (ok) {
    response["sourceUid"] = _tags.source().uid.hex(" ");
    response["message"] = "clone source selected";
  } else {
    response["code"] = "ERR_SOURCE";
    response["message"] = error;
  }
  sendJson(200, toJson(response));
}

void WebApp::handleRead() {
  String error;
  if (!_tags.reread(error)) {
    sendJson(200, errorJson("ERR_READ", error));
    return;
  }
  sendJson(200, tagJson());
}

void WebApp::handleResult() {
  if (!_tags.hasResult()) {
    sendJson(200, errorJson("ERR_NO_RESULT", "no write/clone result yet"));
    return;
  }
  sendJson(200, resultJson(_tags.lastResult()));
}

void WebApp::handleWrite() {
  JsonDocument request;
  const DeserializationError error =
      deserializeJson(request, _server.arg("plain"));
  if (error) {
    sendJson(400, errorJson("ERR_JSON", error.c_str()));
    return;
  }

  std::vector<NdefRecord> records;
  for (JsonObject entry : request["records"].as<JsonArray>()) {
    NdefRecord record;
    record.type = String(entry["type"] | "text");
    record.value = String(entry["value"] | "");
    record.language = String(entry["language"] | "en");
    if (record.value.length() == 0) {
      continue;
    }
    records.push_back(record);
  }

  WriteResult result;
  const bool ok = _tags.writeRecords(records, result);
  broadcastEvent(resultJson(result));
  sendJson(ok ? 200 : 409, resultJson(result));
}

void WebApp::handleClone() {
  JsonDocument request;
  const String body = _server.arg("plain");
  if (body.length() > 0) {
    const DeserializationError error = deserializeJson(request, body);
    if (error) {
      sendJson(400, errorJson("ERR_JSON", error.c_str()));
      return;
    }
  }

  const bool writeUid = request["writeUid"] | false;
  const bool includeTrailers = request["includeTrailers"] | false;

  WriteResult result;
  const bool ok = _tags.clone(writeUid, includeTrailers, result);
  broadcastEvent(resultJson(result));
  sendJson(ok ? 200 : 409, resultJson(result));
}

void WebApp::handleWifiSave() {
  JsonDocument request;
  const DeserializationError error =
      deserializeJson(request, _server.arg("plain"));
  if (error) {
    sendJson(400, errorJson("ERR_JSON", error.c_str()));
    return;
  }

  const String ssid = request["ssid"] | "";
  const String password = request["password"] | "";
  if (ssid.length() == 0) {
    sendJson(400, errorJson("ERR_WIFI", "ssid is required"));
    return;
  }

  _settings.saveWifi(ssid, password);
  logLine(String("[wifi] credentials saved for ") + ssid + ", rebooting");

  JsonDocument response;
  response["ok"] = true;
  response["message"] = "credentials saved, rebooting";
  sendJson(200, toJson(response));
  _restartAtMs = millis() + 1200;
}

void WebApp::handleWifiScan() {
  std::vector<NetworkEntry> networks;
  const int count = WiFi.scanNetworks();
  for (int i = 0; i < count; ++i) {
    networks.push_back(NetworkEntry{WiFi.SSID(i), WiFi.RSSI(i),
                                    static_cast<int32_t>(WiFi.encryptionType(i))});
  }
  std::sort(networks.begin(), networks.end(),
            [](const NetworkEntry& a, const NetworkEntry& b) {
              return a.rssi > b.rssi;
            });

  JsonDocument response;
  response["ok"] = true;
  JsonArray items = response["networks"].to<JsonArray>();
  for (const NetworkEntry& network : networks) {
    JsonObject item = items.add<JsonObject>();
    item["ssid"] = network.ssid;
    item["rssi"] = network.rssi;
    item["encryption"] = network.encryption;
  }
  sendJson(200, toJson(response));
}

void WebApp::handleNotFound() {
  if (_server.uri().startsWith("/api/")) {
    sendJson(404, errorJson("ERR_NOT_FOUND", "unknown endpoint"));
    return;
  }
  if (LittleFS.exists("/index.html")) {
    _server.send(200, "text/html", LittleFS.open("/index.html", "r").readString());
    return;
  }
  _server.send(200, "text/plain",
               "Web UI not uploaded. Run: pio run -t uploadfs");
}

void WebApp::sendJson(int code, const String& body) {
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(code, "application/json", body);
}

void WebApp::broadcastEvent(String json) { _ws.broadcastTXT(json); }

void WebApp::logLine(const String& message) {
  Serial.println(message);
  _logs.push_back(message);
  if (_logs.size() > 80) {
    _logs.erase(_logs.begin());
  }
  broadcastEvent(logJson(message));
}

String WebApp::statusJson() const {
  JsonDocument doc;
  doc["event"] = "status";
  doc["ok"] = true;
  doc["version"] = APP_VERSION;

  const uint32_t firmware = _tags.reader().firmwareVersion();
  char firmwareText[16];
  snprintf(firmwareText, sizeof(firmwareText), "%u.%u",
           static_cast<unsigned>((firmware >> 16) & 0xFF),
           static_cast<unsigned>((firmware >> 8) & 0xFF));
  doc["firmware"] = firmwareText;

  doc["bus"] = _tags.reader().busName();
  doc["readerReady"] = _tags.readerReady();
  doc["present"] = _tags.present();
  doc["busy"] = _tags.busy();
  doc["uid"] = _tags.snapshot().uid.hex(" ");
  doc["armed"] = _tags.armed();
  doc["hasSource"] = _tags.hasSource();
  doc["sourceUid"] = _tags.source().uid.hex(" ");
  doc["historyCount"] = _tags.history().size();
  doc["scans"] = _tags.scans();
  doc["reads"] = _tags.reader().reads();
  doc["misses"] = _tags.reader().misses();
  doc["busErrors"] = _tags.reader().busErrors();

  doc["mode"] = _net.modeName();
  doc["ip"] = _net.ip();
  doc["ssid"] = _net.ssid();
  doc["rssi"] = _net.rssi();
  doc["ap"] = _net.apActive();
  doc["uptimeMs"] = millis();
  doc["heap"] = ESP.getFreeHeap();
  return toJson(doc);
}

String WebApp::tagJson() const {
  JsonDocument doc;
  doc["event"] = "tag";
  doc["ok"] = true;
  doc["present"] = _tags.present();
  fillTag(doc["tag"].to<JsonObject>(), _tags.snapshot());
  return toJson(doc);
}

String WebApp::historyJson() const {
  JsonDocument doc;
  doc["ok"] = true;
  JsonArray items = doc["items"].to<JsonArray>();
  for (const TagSnapshot& snapshot : _tags.history()) {
    JsonObject item = items.add<JsonObject>();
    item["uid"] = snapshot.uid.hex(" ");
    item["uidCompact"] = snapshot.uid.hex();
    item["model"] = snapshot.model;
    item["kind"] = snapshot.kindName();
    item["capacity"] = snapshot.capacity;
    item["records"] = snapshot.records.size();
    item["ageMs"] = millis() - snapshot.readAtMs;
  }
  return toJson(doc);
}

String WebApp::resultJson(const WriteResult& result) const {
  JsonDocument doc;
  doc["event"] = "result";
  doc["ok"] = result.ok;
  doc["armed"] = result.armed;
  doc["code"] = result.code;
  doc["message"] = result.message;
  doc["bytesWritten"] = result.bytesWritten;
  doc["fieldsMatched"] = result.fieldsMatched;
  doc["fieldsTotal"] = result.fieldsTotal;
  doc["uidAttempted"] = result.uidAttempted;
  doc["uidLocked"] = result.uidLocked;
  doc["uidMatch"] = result.uidMatch;
  doc["recordsMatch"] = result.recordsMatch;

  JsonArray mismatches = doc["mismatches"].to<JsonArray>();
  for (const String& mismatch : result.mismatches) {
    mismatches.add(mismatch);
  }
  JsonArray notes = doc["notes"].to<JsonArray>();
  for (const String& note : result.notes) {
    notes.add(note);
  }
  return toJson(doc);
}

String WebApp::errorJson(const String& code, const String& message) const {
  JsonDocument doc;
  doc["ok"] = false;
  doc["code"] = code;
  doc["message"] = message;
  return toJson(doc);
}
