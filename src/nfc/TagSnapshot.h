#pragma once

#include <Arduino.h>

#include <vector>

#include "nfc/Ndef.h"
#include "nfc/TagInfo.h"

enum class TagKind : uint8_t { Unknown, Type2, MifareClassic };

struct TagSnapshot {
  TagKind kind = TagKind::Unknown;
  TagUid uid;
  String model;
  uint16_t capacity = 0;   // usable data bytes
  uint16_t totalBytes = 0;  // dumped memory size
  std::vector<uint8_t> memory;
  std::vector<NdefRecord> records;
  bool ndefFound = false;
  bool complete = true;
  String error;
  uint32_t readAtMs = 0;

  void reset() {
    kind = TagKind::Unknown;
    uid.clear();
    model = "";
    capacity = 0;
    totalBytes = 0;
    memory.clear();
    records.clear();
    ndefFound = false;
    complete = true;
    error = "";
    readAtMs = 0;
  }

  String kindName() const {
    switch (kind) {
      case TagKind::Type2:
        return "Type 2";
      case TagKind::MifareClassic:
        return "Mifare Classic";
      default:
        return "unknown";
    }
  }
};
