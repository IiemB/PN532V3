#pragma once

#include <Arduino.h>

#if !defined(ESP8266)
#include <Preferences.h>
#endif

class Settings {
 public:
  void begin();

  bool hasWifi() const { return _ssid.length() > 0; }
  const String& wifiSsid() const { return _ssid; }
  const String& wifiPassword() const { return _password; }

  void saveWifi(const String& ssid, const String& password);
  void clearWifi();

 private:
  void store();

#if !defined(ESP8266)
  Preferences _prefs;
#endif
  String _ssid;
  String _password;
};
