#pragma once

#include <Arduino.h>

#include <vector>

struct NdefRecord {
  String type;  // "text", "uri", "other"
  String value;
  String language;  // text records only
  bool utf16 = false;
  String tnf;  // original TNF/type label for "other" records
};

namespace ndef {

bool parseTlv(const uint8_t* data, size_t length,
              std::vector<NdefRecord>& records);

// Builds a complete NDEF message (records with MB/ME flags) for text/uri
// entries of `records`. Unsupported record types are skipped.
bool encodeMessage(const std::vector<NdefRecord>& records,
                   std::vector<uint8_t>& message);

// Wraps an NDEF message in a Type 2 Tag TLV (0x03 ... 0xFE).
bool wrapTlv(const std::vector<uint8_t>& message, std::vector<uint8_t>& tlv);

String uriPrefix(uint8_t code);
uint8_t bestUriPrefix(const String& uri, String& suffix);

}  // namespace ndef
