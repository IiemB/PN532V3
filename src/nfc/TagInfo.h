#pragma once

#include <Arduino.h>

struct TagUid {
  static constexpr uint8_t kMaxLength = 10;

  uint8_t bytes[kMaxLength] = {};
  uint8_t length = 0;
  uint16_t atqa = 0;  // SENS_RES from anticollision
  uint8_t sak = 0;    // SEL_RES from anticollision

  bool valid() const { return length > 0; }

  void clear() {
    length = 0;
    atqa = 0;
    sak = 0;
    memset(bytes, 0, sizeof(bytes));
  }

  bool equals(const TagUid& other) const {
    return length == other.length && memcmp(bytes, other.bytes, length) == 0;
  }

  String hex(const char* separator = "") const {
    String out;
    out.reserve(static_cast<size_t>(length) * (2 + strlen(separator)));
    char buf[3];
    for (uint8_t i = 0; i < length; ++i) {
      if (i > 0) {
        out += separator;
      }
      snprintf(buf, sizeof(buf), "%02X", bytes[i]);
      out += buf;
    }
    return out;
  }
};
