#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "nfc/Ndef.h"

static void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
  std::cout << "ok: " << message << "\n";
}

int main() {
  {
    std::vector<NdefRecord> records;
    NdefRecord text;
    text.type = "text";
    text.value = "hello tag";
    text.language = "en";
    NdefRecord uri;
    uri.type = "uri";
    uri.value = "https://www.example.com/a?b=c";
    records.push_back(text);
    records.push_back(uri);

    std::vector<uint8_t> message;
    expect(ndef::encodeMessage(records, message), "encode text+uri message");
    std::vector<uint8_t> tlv;
    expect(ndef::wrapTlv(message, tlv), "wrap tlv");
    expect(tlv.front() == 0x03, "tlv starts with 0x03");
    expect(tlv.back() == 0xFE, "tlv ends with 0xFE");

    std::vector<NdefRecord> parsed;
    expect(ndef::parseTlv(tlv.data(), tlv.size(), parsed), "parse round trip");
    expect(parsed.size() == 2, "two records parsed");
    expect(parsed[0].type == "text" && parsed[0].value == "hello tag",
           "text record matches");
    expect(parsed[0].language == "en", "text language matches");
    expect(parsed[1].type == "uri" &&
               parsed[1].value == "https://www.example.com/a?b=c",
           "uri record matches");
  }

  {
    NdefRecord text;
    text.type = "text";
    text.value = std::string(400, 'x').c_str();
    std::vector<NdefRecord> records{text};
    std::vector<uint8_t> message;
    expect(ndef::encodeMessage(records, message), "encode 400 byte text");
    expect(message.size() > 255, "long record uses extended length");
    expect((message[0] & 0x10) == 0, "long record clears SR flag");

    std::vector<uint8_t> tlv;
    expect(ndef::wrapTlv(message, tlv), "wrap long tlv");
    expect(tlv[1] == 0xFF, "long tlv uses 0xFF length marker");

    std::vector<NdefRecord> parsed;
    expect(ndef::parseTlv(tlv.data(), tlv.size(), parsed), "parse long tlv");
    expect(parsed.size() == 1 && parsed[0].value == text.value,
           "long text round trips");
  }

  {
    // UTF-16LE text record: status 0x82, lang "en", "hi"
    const uint8_t message[] = {0xD1, 0x01, 0x07, 0x54, 0x82,
                               'e',  'n',  0x68, 0x00, 0x69, 0x00};
    std::vector<uint8_t> tlv{0x03, sizeof(message)};
    tlv.insert(tlv.end(), message, message + sizeof(message));
    tlv.push_back(0xFE);

    std::vector<NdefRecord> parsed;
    expect(ndef::parseTlv(tlv.data(), tlv.size(), parsed), "parse utf16 tlv");
    expect(parsed.size() == 1, "utf16 one record");
    expect(parsed[0].utf16, "utf16 flag set");
    expect(parsed[0].value == "hi", "utf16 converted to utf8");
  }

  {
    NdefRecord uri;
    uri.type = "uri";
    uri.value = "https://www.example.com";
    std::vector<NdefRecord> records{uri};
    std::vector<uint8_t> message;
    expect(ndef::encodeMessage(records, message), "encode https uri");
    expect(message[3] == 'U', "uri record type is U");
    expect(message[4] == 0x02, "https://www. prefix chosen");
    expect(message[5] == 'e', "prefix stripped from payload");
  }

  {
    const uint8_t buffer[] = {0x03, 0x05, 0xD1, 0x01, 0x54, 0x00, 0xFE};
    std::vector<NdefRecord> parsed;
    expect(!ndef::parseTlv(buffer, sizeof(buffer), parsed),
           "reject truncated record");
  }

  {
    NdefRecord bad;
    bad.type = "other";
    bad.value = "x";
    std::vector<NdefRecord> records{bad};
    std::vector<uint8_t> message;
    expect(!ndef::encodeMessage(records, message),
           "reject unsupported record type");
    expect(message.empty(), "failed encode leaves empty message");
  }

  std::cout << "ALL TESTS PASSED\n";
  return 0;
}
