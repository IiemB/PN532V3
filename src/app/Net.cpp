#include "app/Net.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#include <WiFi.h>
#endif

namespace {

constexpr const char* kHostname = "pn532";
constexpr const char* kApSsid = "PN532-Setup";
constexpr uint32_t kStaConnectTimeoutMs = 12000;
constexpr uint32_t kApFallbackDelayMs = 30000;
constexpr uint32_t kRetryIntervalMs = 30000;

}  // namespace

void Net::begin(Settings& settings) {
  _settings = &settings;
  _bootMs = millis();
  _staConfigured = settings.hasWifi();

  if (_staConfigured) {
    WiFi.mode(WIFI_STA);
#if !defined(ESP8266)
    WiFi.setHostname(kHostname);
#endif
    connectStation();
  }

  if (!connected()) {
    startAccessPoint();
  }

  if (connected()) {
    _mdnsStarted = MDNS.begin(kHostname);
    if (_mdnsStarted) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[net] http://%s.local/\n", kHostname);
    }
  }
}

void Net::connectStation() {
  WiFi.begin(_settings->wifiSsid().c_str(),
             _settings->wifiPassword().c_str());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < kStaConnectTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (connected()) {
    _lastConnectedMs = millis();
    Serial.printf("[net] connected to %s, ip=%s\n",
                  _settings->wifiSsid().c_str(), ip().c_str());
  } else {
    Serial.println("[net] station connection failed, starting setup AP");
  }
}

void Net::startAccessPoint() {
  if (_apActive) {
    return;
  }
  if (_staConfigured) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_AP);
  }
  WiFi.softAP(kApSsid);
  _apActive = true;
  Serial.printf("[net] setup AP \"%s\" at %s\n", kApSsid,
                WiFi.softAPIP().toString().c_str());
}

void Net::loop() {
#if defined(ESP8266)
  if (_mdnsStarted) {
    MDNS.update();
  }
#endif
  if (!_staConfigured) {
    return;
  }
  if (!connected()) {
    if (!_apActive && millis() - _bootMs > kApFallbackDelayMs) {
      startAccessPoint();
    }
    if (millis() - _lastRetryMs > kRetryIntervalMs) {
      _lastRetryMs = millis();
      WiFi.reconnect();
    }
    return;
  }

  _lastConnectedMs = millis();
  if (!_mdnsStarted) {
    _mdnsStarted = MDNS.begin(kHostname);
    if (_mdnsStarted) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[net] http://%s.local/\n", kHostname);
    }
  }
  if (_apActive) {
    // Keep the setup AP reachable while the station link is up so the network
    // can be reconfigured without a serial console.
    WiFi.mode(WIFI_AP_STA);
  }
}

bool Net::connected() const { return WiFi.status() == WL_CONNECTED; }

String Net::ip() const {
  return connected() ? WiFi.localIP().toString()
                     : WiFi.softAPIP().toString();
}

String Net::ssid() const {
  return connected() ? WiFi.SSID() : String(kApSsid);
}

int32_t Net::rssi() const { return connected() ? WiFi.RSSI() : 0; }

String Net::modeName() const {
  if (connected() && _apActive) {
    return "STA+AP";
  }
  return connected() ? "STA" : "AP";
}
