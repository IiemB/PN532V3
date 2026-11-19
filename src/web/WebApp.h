#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>

#if defined(ESP8266)
#include <ESP8266WebServer.h>
using WebServer = ESP8266WebServer;
#else
#include <WebServer.h>
#endif

#include <vector>

#include "app/Net.h"
#include "app/Settings.h"
#include "app/TagService.h"

class WebApp {
 public:
  WebApp(TagService& tags, Settings& settings, Net& net)
      : _tags(tags), _settings(settings), _net(net) {}

  void begin();
  void loop();

 private:
  void setupRoutes();
  void onWebSocketEvent(uint8_t client, WStype_t type, uint8_t* payload,
                        size_t length);

  void handleStatus();
  void handleTag();
  void handleHistory();
  void handleSource();
  void handleRead();
  void handleResult();
  void handleWrite();
  void handleClone();
  void handleWifiSave();
  void handleWifiScan();
  void handleNotFound();

  void sendJson(int code, const String& body);
  void broadcastEvent(String json);
  void logLine(const String& message);

  String statusJson() const;
  String tagJson() const;
  String historyJson() const;
  String resultJson(const WriteResult& result) const;
  String errorJson(const String& code, const String& message) const;

  TagService& _tags;
  Settings& _settings;
  Net& _net;
  WebServer _server{80};
  WebSocketsServer _ws{81};
  std::vector<String> _logs;
  uint32_t _restartAtMs = 0;
};
