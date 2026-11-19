#include "app/Settings.h"

#if defined(ESP8266)
#include <EEPROM.h>

namespace {

constexpr int kEepromSize = 256;
constexpr uint16_t kMagic = 0x504E;  // "PN"

struct Blob {
  uint16_t magic;
  char ssid[33];
  char password[65];
};

}  // namespace

void Settings::begin() {
  EEPROM.begin(kEepromSize);
  Blob blob{};
  EEPROM.get(0, blob);
  if (blob.magic != kMagic) {
    _ssid = "";
    _password = "";
    return;
  }
  blob.ssid[sizeof(blob.ssid) - 1] = 0;
  blob.password[sizeof(blob.password) - 1] = 0;
  _ssid = blob.ssid;
  _password = blob.password;
}

void Settings::store() {
  Blob blob{};
  blob.magic = kMagic;
  strncpy(blob.ssid, _ssid.c_str(), sizeof(blob.ssid) - 1);
  strncpy(blob.password, _password.c_str(), sizeof(blob.password) - 1);
  EEPROM.put(0, blob);
  EEPROM.commit();
}

#else

namespace {
constexpr const char* kNamespace = "pn532v3";
}

void Settings::begin() {
  _prefs.begin(kNamespace, false);
  _ssid = _prefs.getString("ssid", "");
  _password = _prefs.getString("pass", "");
}

void Settings::store() {
  _prefs.putString("ssid", _ssid);
  _prefs.putString("pass", _password);
}

#endif

void Settings::saveWifi(const String& ssid, const String& password) {
  _ssid = ssid;
  _password = password;
  store();
}

void Settings::clearWifi() {
  _ssid = "";
  _password = "";
  store();
}
