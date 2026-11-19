#include "app/Scanner.h"

#include "config.h"

void Scanner::poll() {
  const uint32_t now = millis();
  if (now - _lastPoll < TAG_POLL_INTERVAL_MS) {
    return;
  }
  _lastPoll = now;

  TagUid uid;
  if (_reader.readTarget(uid)) {
    _lastSeen = now;
    if (_present && _current.equals(uid)) {
      return;
    }
    if (_present) {
      // A different tag was placed without the previous one timing out.
      emitRemoved(_current);
    }
    _current = uid;
    _present = true;
    ++_detections;
    emitDetected(_current);
    return;
  }

  if (_present && now - _lastSeen > TAG_REMOVE_TIMEOUT_MS) {
    _present = false;
    emitRemoved(_current);
  }
}

void Scanner::emitDetected(const TagUid& uid) {
  Serial.printf("[tag] detected uid=%s\n", uid.hex(" ").c_str());
  if (_onDetected) {
    _onDetected(uid);
  }
}

void Scanner::emitRemoved(const TagUid& uid) {
  Serial.printf("[tag] removed uid=%s\n", uid.hex(" ").c_str());
  if (_onRemoved) {
    _onRemoved(uid);
  }
}
