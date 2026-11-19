#pragma once

#include <Arduino.h>

#include "app/Settings.h"

class Net {
 public:
  void begin(Settings& settings);
  void loop();

  bool connected() const;
  bool apActive() const { return _apActive; }
  String ip() const;
  String ssid() const;
  int32_t rssi() const;
  String modeName() const;

 private:
  void startAccessPoint();
  void connectStation();

  Settings* _settings = nullptr;
  bool _apActive = false;
  bool _staConfigured = false;
  bool _mdnsStarted = false;
  uint32_t _bootMs = 0;
  uint32_t _lastRetryMs = 0;
  uint32_t _lastConnectedMs = 0;
};
