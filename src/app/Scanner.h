#pragma once

#include <Arduino.h>

#include <functional>

#include "nfc/NfcReader.h"
#include "nfc/TagInfo.h"

class Scanner {
 public:
  using TagCallback = std::function<void(const TagUid&)>;

  explicit Scanner(NfcReader& reader) : _reader(reader) {}

  void setOnDetected(TagCallback callback) { _onDetected = std::move(callback); }
  void setOnRemoved(TagCallback callback) { _onRemoved = std::move(callback); }

  void poll();

  bool tagPresent() const { return _present; }
  const TagUid& currentUid() const { return _current; }
  uint32_t detections() const { return _detections; }
  uint32_t lastSeenMs() const { return _lastSeen; }

 private:
  void emitDetected(const TagUid& uid);
  void emitRemoved(const TagUid& uid);

  NfcReader& _reader;
  TagUid _current;
  bool _present = false;
  uint32_t _lastSeen = 0;
  uint32_t _lastPoll = 0;
  uint32_t _detections = 0;
  TagCallback _onDetected;
  TagCallback _onRemoved;
};
