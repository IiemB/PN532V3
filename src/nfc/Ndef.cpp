#include "nfc/Ndef.h"

namespace {

const char* const kUriPrefixes[] = {
    "",          // 0x00
    "http://www.", "https://www.", "http://", "https://",
    "tel:",      "mailto:",        "ftp://anonymous:anonymous@",
    "ftp://ftp.", "ftps://",        "sftp://", "smb://",  "nfs://",
    "ftp://",    "dav://",          "news:",   "telnet://",
    "imap:",     "rtsp://",         "urn:",    "pop:",
    "sip:",      "sips:",           "tftp:",   "btspp://",
    "btl2cap://", "btgoep://",      "tcpobex://", "irdaobex://",
    "file://",   "urn:epc:id:",     "urn:epc:tag:", "urn:epc:pat:",
    "urn:epc:raw:", "urn:epc:",     "urn:nfc:",
};

constexpr uint8_t kUriPrefixCount = sizeof(kUriPrefixes) / sizeof(kUriPrefixes[0]);

void appendUtf8(String& out, uint32_t codePoint) {
  if (codePoint < 0x80) {
    out += static_cast<char>(codePoint);
  } else if (codePoint < 0x800) {
    out += static_cast<char>(0xC0 | (codePoint >> 6));
    out += static_cast<char>(0x80 | (codePoint & 0x3F));
  } else if (codePoint < 0x10000) {
    out += static_cast<char>(0xE0 | (codePoint >> 12));
    out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codePoint & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (codePoint >> 18));
    out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codePoint & 0x3F));
  }
}

String utf16LeToUtf8(const uint8_t* data, size_t length) {
  String out;
  out.reserve(length);
  for (size_t i = 0; i + 1 < length; i += 2) {
    uint32_t codePoint = static_cast<uint32_t>(data[i]) |
                         (static_cast<uint32_t>(data[i + 1]) << 8);
    if (codePoint >= 0xD800 && codePoint <= 0xDBFF && i + 3 < length) {
      const uint32_t low = static_cast<uint32_t>(data[i + 2]) |
                           (static_cast<uint32_t>(data[i + 3]) << 8);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
        i += 2;
      }
    }
    appendUtf8(out, codePoint);
  }
  return out;
}

String stringFromBytes(const uint8_t* data, size_t length) {
  String out;
  out.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    out += static_cast<char>(data[i]);
  }
  return out;
}

String hexOf(const uint8_t* data, size_t length) {
  String out;
  out.reserve(length * 2);
  char buf[3];
  for (size_t i = 0; i < length; ++i) {
    snprintf(buf, sizeof(buf), "%02X", data[i]);
    out += buf;
  }
  return out;
}

bool parseMessage(const uint8_t* message, size_t length,
                  std::vector<NdefRecord>& records) {
  size_t i = 0;
  while (i < length) {
    const uint8_t header = message[i++];
    if (header & 0x20) {
      return false;  // chunked records are not produced by tag writers in v1
    }
    const bool shortRecord = (header & 0x10) != 0;
    const bool hasIdLength = (header & 0x08) != 0;
    const uint8_t tnf = header & 0x07;

    if (i >= length) {
      return false;
    }
    const uint8_t typeLength = message[i++];

    uint32_t payloadLength = 0;
    if (shortRecord) {
      if (i >= length) {
        return false;
      }
      payloadLength = message[i++];
    } else {
      if (i + 4 > length) {
        return false;
      }
      payloadLength = (static_cast<uint32_t>(message[i]) << 24) |
                      (static_cast<uint32_t>(message[i + 1]) << 16) |
                      (static_cast<uint32_t>(message[i + 2]) << 8) |
                      static_cast<uint32_t>(message[i + 3]);
      i += 4;
    }

    uint8_t idLength = 0;
    if (hasIdLength) {
      if (i >= length) {
        return false;
      }
      idLength = message[i++];
    }

    if (i + typeLength + idLength + payloadLength > length) {
      return false;
    }

    const uint8_t* type = message + i;
    i += typeLength;
    i += idLength;
    const uint8_t* payload = message + i;
    i += payloadLength;

    NdefRecord record;
    if (tnf == 0x01 && typeLength == 1 && type[0] == 'T' && payloadLength >= 1) {
      record.type = "text";
      const uint8_t status = payload[0];
      record.utf16 = (status & 0x80) != 0;
      const uint8_t languageLength = status & 0x3F;
      if (static_cast<uint32_t>(1 + languageLength) > payloadLength) {
        return false;
      }
      record.language = stringFromBytes(payload + 1, languageLength);
      const uint8_t* text = payload + 1 + languageLength;
      const size_t textLength = payloadLength - 1 - languageLength;
      record.value = record.utf16 ? utf16LeToUtf8(text, textLength)
                                  : stringFromBytes(text, textLength);
    } else if (tnf == 0x01 && typeLength == 1 && type[0] == 'U' &&
               payloadLength >= 1) {
      record.type = "uri";
      String value = ndef::uriPrefix(payload[0]);
      value += stringFromBytes(payload + 1, payloadLength - 1);
      record.value = value;
    } else {
      record.type = "other";
      record.tnf = tnf == 0x01 ? stringFromBytes(type, typeLength)
                               : String("tnf") + String(tnf);
      record.value = hexOf(payload, payloadLength);
    }

    records.push_back(record);
    if (header & 0x40) {
      break;  // message end
    }
  }
  return !records.empty();
}

uint8_t headerFor(bool first, bool last, bool shortRecord) {
  uint8_t header = 0x01;  // TNF well-known
  if (first) {
    header |= 0x80;
  }
  if (last) {
    header |= 0x40;
  }
  if (shortRecord) {
    header |= 0x10;
  }
  return header;
}

void appendLength(std::vector<uint8_t>& out, size_t length) {
  out.push_back(static_cast<uint8_t>(length));
}

}  // namespace

namespace ndef {

String uriPrefix(uint8_t code) {
  return code < kUriPrefixCount ? String(kUriPrefixes[code]) : String();
}

uint8_t bestUriPrefix(const String& uri, String& suffix) {
  uint8_t best = 0;
  size_t bestLength = 0;
  for (uint8_t code = 1; code < kUriPrefixCount; ++code) {
    const String prefix(kUriPrefixes[code]);
    if (uri.startsWith(prefix) && prefix.length() > bestLength) {
      best = code;
      bestLength = prefix.length();
    }
  }
  suffix = uri.substring(bestLength);
  return best;
}

bool parseTlv(const uint8_t* data, size_t length,
              std::vector<NdefRecord>& records) {
  records.clear();
  size_t i = 0;
  while (i < length) {
    const uint8_t type = data[i++];
    if (type == 0x00) {
      continue;  // NULL TLV
    }
    if (type == 0xFE) {
      break;  // terminator
    }
    if (i >= length) {
      return false;
    }
    uint32_t tlvLength = data[i++];
    if (tlvLength == 0xFF) {
      if (i + 2 > length) {
        return false;
      }
      tlvLength = (static_cast<uint32_t>(data[i]) << 8) | data[i + 1];
      i += 2;
    }
    if (i + tlvLength > length) {
      return false;
    }
    if (type == 0x03) {
      return parseMessage(data + i, tlvLength, records);
    }
    i += tlvLength;
  }
  return false;
}

bool encodeMessage(const std::vector<NdefRecord>& records,
                   std::vector<uint8_t>& message) {
  message.clear();
  if (records.empty()) {
    return false;
  }

  for (size_t index = 0; index < records.size(); ++index) {
    const NdefRecord& record = records[index];
    std::vector<uint8_t> payload;
    uint8_t typeChar = 0;

    if (record.type == "text") {
      typeChar = 'T';
      String language = record.language.length() ? record.language : "en";
      if (language.length() > 0x3F) {
        return false;
      }
      payload.push_back(static_cast<uint8_t>(language.length()));
      for (size_t i = 0; i < language.length(); ++i) {
        payload.push_back(static_cast<uint8_t>(language[i]));
      }
      for (size_t i = 0; i < record.value.length(); ++i) {
        payload.push_back(static_cast<uint8_t>(record.value[i]));
      }
    } else if (record.type == "uri") {
      typeChar = 'U';
      String suffix;
      const uint8_t prefix = bestUriPrefix(record.value, suffix);
      payload.push_back(prefix);
      for (size_t i = 0; i < suffix.length(); ++i) {
        payload.push_back(static_cast<uint8_t>(suffix[i]));
      }
    } else {
      return false;
    }

    const bool shortRecord = payload.size() <= 0xFF;
    message.push_back(headerFor(index == 0, index + 1 == records.size(),
                                shortRecord));
    message.push_back(1);  // type length
    if (shortRecord) {
      message.push_back(static_cast<uint8_t>(payload.size()));
    } else {
      const uint32_t length = static_cast<uint32_t>(payload.size());
      message.push_back(static_cast<uint8_t>((length >> 24) & 0xFF));
      message.push_back(static_cast<uint8_t>((length >> 16) & 0xFF));
      message.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
      message.push_back(static_cast<uint8_t>(length & 0xFF));
    }
    message.push_back(typeChar);
    message.insert(message.end(), payload.begin(), payload.end());
  }
  return true;
}

bool wrapTlv(const std::vector<uint8_t>& message, std::vector<uint8_t>& tlv) {
  tlv.clear();
  tlv.push_back(0x03);
  if (message.size() <= 0xFE) {
    appendLength(tlv, message.size());
  } else if (message.size() <= 0xFFFF) {
    tlv.push_back(0xFF);
    tlv.push_back(static_cast<uint8_t>((message.size() >> 8) & 0xFF));
    tlv.push_back(static_cast<uint8_t>(message.size() & 0xFF));
  } else {
    return false;
  }
  tlv.insert(tlv.end(), message.begin(), message.end());
  tlv.push_back(0xFE);
  return true;
}

}  // namespace ndef
