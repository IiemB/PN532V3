#include "app/TagService.h"

#include <algorithm>

#include "config.h"

namespace {

constexpr uint32_t kInitRetryMs = 3000;
constexpr uint32_t kHealthCheckMs = 15000;
#if defined(ESP8266)
constexpr size_t kHistoryLimit = 4;
#else
constexpr size_t kHistoryLimit = 8;
#endif

String compactUid(const String& uid) {
  String out;
  out.reserve(uid.length());
  for (size_t i = 0; i < uid.length(); ++i) {
    const char c = uid[i];
    if (c != ' ' && c != ':' && c != '-') {
      out += static_cast<char>(toupper(c));
    }
  }
  return out;
}

}  // namespace

void TagService::begin() {
  _scanner.setOnDetected([this](const TagUid& uid) { onTagDetected(uid); });
  _scanner.setOnRemoved([this](const TagUid& uid) { onTagRemoved(uid); });
  retryInit();
}

void TagService::loop() {
  if (!_nfc.isReady()) {
    retryInit();
    delay(25);
    return;
  }

  const uint32_t now = millis();
  if (now - _lastHealthMs >= kHealthCheckMs) {
    _lastHealthMs = now;
    _nfc.healthCheck();
  }

  if (_busy) {
    return;
  }
  _scanner.poll();
}

void TagService::retryInit() {
  const uint32_t now = millis();
  if (_lastInitMs != 0 && now - _lastInitMs < kInitRetryMs) {
    return;
  }
  _lastInitMs = now;
  if (_nfc.begin()) {
    log("[nfc] reader online");
  } else {
    log("[nfc] reader offline; check wiring and DIP switches");
  }
  _lastHealthMs = millis();
}

void TagService::onTagDetected(const TagUid& uid) {
  TagSnapshot snapshot;
  const bool readOk = _tagReader.read(uid, snapshot);
  _snapshot = snapshot;
  _present = true;
  if (readOk) {
    rememberSnapshot(_snapshot);
    log(String("[tag] ") + _snapshot.model + ", " + _snapshot.records.size() +
        " NDEF record(s)");
  } else {
    log(String("[tag] read failed: ") + snapshot.error);
  }

  if (_armed) {
    _armed = false;
    _busy = true;
    WriteResult result;
    if (_source.uid.equals(_snapshot.uid) &&
        _source.memory == _snapshot.memory) {
      result.code = "ERR_SAME_TAG";
      result.message = "source and target are identical; nothing to clone";
    } else {
      result.armed = true;
      _writer.clone(_source, _snapshot, _armedOptions, result);
    }
    String error;
    refreshSnapshot(error);
    _busy = false;
    _lastResult = result;
    _hasResult = true;
    log(String("[clone] ") + (result.ok ? "ok" : result.code) + ": " +
        result.message);
    if (_onResult) {
      _onResult(_lastResult);
    }
    return;
  }

  if (_onSnapshot) {
    _onSnapshot(_snapshot);
  }
}

void TagService::onTagRemoved(const TagUid& uid) {
  _present = false;
  log(String("[tag] removed ") + uid.hex(" "));
  if (_onRemoved) {
    _onRemoved(uid);
  }
}

bool TagService::waitForTag(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (!_present && millis() - start < timeoutMs) {
    _scanner.poll();
    delay(10);
  }
  return _present;
}

bool TagService::refreshSnapshot(String& error) {
  TagUid uid;
  if (!_nfc.readTarget(uid, 400)) {
    _present = false;
    error = "no tag detected";
    return false;
  }

  TagSnapshot snapshot;
  if (!_tagReader.read(uid, snapshot)) {
    _present = false;
    error = snapshot.error;
    return false;
  }
  _snapshot = snapshot;
  _present = true;
  rememberSnapshot(_snapshot);
  if (_onSnapshot) {
    _onSnapshot(_snapshot);
  }
  return true;
}

bool TagService::setSourceCurrent(String& error) {
  if (!_present && !waitForTag(4000)) {
    error = "no tag on the reader";
    return false;
  }
  if (!_snapshot.uid.valid()) {
    error = "tag snapshot is not ready";
    return false;
  }
  _source = _snapshot;
  _hasSource = true;
  log(String("[clone] source set to ") + _source.uid.hex(" "));
  return true;
}

bool TagService::setSourceFromUid(const String& uid, String& error) {
  const String wanted = compactUid(uid);
  for (const TagSnapshot& snapshot : _history) {
    if (snapshot.uid.hex().equalsIgnoreCase(wanted)) {
      _source = snapshot;
      _hasSource = true;
      log(String("[clone] source set to ") + _source.uid.hex(" "));
      return true;
    }
  }
  error = "uid not found in history";
  return false;
}

bool TagService::clone(bool writeUid, bool includeTrailers,
                       WriteResult& result) {
  result = WriteResult();
  if (!_hasSource) {
    result.code = "ERR_NO_SOURCE";
    result.message = "select a source tag first";
    return false;
  }

  if (!_present) {
    _armed = true;
    _armedOptions.writeUid = writeUid;
    _armedOptions.includeTrailers = includeTrailers;
    _lastResult = WriteResult();
    _lastResult.ok = true;
    _lastResult.armed = true;
    _lastResult.code = "ARMED";
    _lastResult.message = "waiting for the target tag";
    _hasResult = false;
    log("[clone] armed - waiting for the target tag");
    result = _lastResult;
    return true;
  }

  _busy = true;
  String error;
  if (!refreshSnapshot(error)) {
    _busy = false;
    result.code = "ERR_NO_TAG";
    result.message = error;
    return false;
  }
  if (_source.uid.equals(_snapshot.uid) &&
      _source.memory == _snapshot.memory) {
    _busy = false;
    result.code = "ERR_SAME_TAG";
    result.message = "source and target are identical; nothing to clone";
    return false;
  }

  log(String("[clone] writing ") + _source.uid.hex(" ") + " -> " +
      _snapshot.uid.hex(" "));
  CloneOptions options;
  options.writeUid = writeUid;
  options.includeTrailers = includeTrailers;
  const bool ok = _writer.clone(_source, _snapshot, options, result);
  String refreshError;
  refreshSnapshot(refreshError);
  _busy = false;

  _lastResult = result;
  _hasResult = true;
  log(String("[clone] ") + (ok ? "ok" : result.code) + ": " + result.message);
  return ok;
}

bool TagService::writeRecords(const std::vector<NdefRecord>& records,
                              WriteResult& result) {
  result = WriteResult();
  if (records.empty()) {
    result.code = "ERR_NDEF";
    result.message = "no records supplied";
    return false;
  }

  _busy = true;
  if (!waitForTag(10000)) {
    _busy = false;
    result.code = "ERR_NO_TAG";
    result.message = "place a tag on the reader";
    return false;
  }
  const bool ok = _writer.writeRecords(_snapshot, records, result);
  String error;
  refreshSnapshot(error);
  _busy = false;

  _lastResult = result;
  _hasResult = true;
  log(String("[write] ") + (ok ? "ok" : result.code) + ": " + result.message);
  return ok;
}

bool TagService::reread(String& error) {
  if (!_present && !_snapshot.uid.valid()) {
    error = "no tag on the reader";
    return false;
  }
  _busy = true;
  const bool ok = refreshSnapshot(error);
  _busy = false;
  return ok;
}

void TagService::rememberSnapshot(const TagSnapshot& snapshot) {
  for (auto it = _history.begin(); it != _history.end(); ++it) {
    if (it->uid.equals(snapshot.uid)) {
      _history.erase(it);
      break;
    }
  }
  _history.insert(_history.begin(), snapshot);
  if (_history.size() > kHistoryLimit) {
    _history.resize(kHistoryLimit);
  }
}

void TagService::log(const String& message) {
  Serial.println(message);
  if (_onLog) {
    _onLog(message);
  }
}
