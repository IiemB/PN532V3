#include <Arduino.h>

#include "app/Net.h"
#include "app/Settings.h"
#include "app/TagService.h"
#include "config.h"
#include "web/WebApp.h"

namespace {

Settings settings;
Net net;
TagService tags;
WebApp web(tags, settings, net);

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  const uint32_t waitStart = millis();
  while (!Serial && millis() - waitStart < 3000) {
    delay(10);
  }
  delay(200);

#if defined(ESP8266)
  Serial.printf("\nPN532V3 %s (NodeMCU ESP8266)\n", APP_VERSION);
#else
  Serial.printf("\nPN532V3 %s (ESP32-C3 SuperMini)\n", APP_VERSION);
#endif

  settings.begin();
  net.begin(settings);
  tags.begin();
  web.begin();

  Serial.printf("[sys] ready, free heap %u bytes\n", ESP.getFreeHeap());
}

void loop() {
  tags.loop();
  net.loop();
  web.loop();
  delay(1);
}
