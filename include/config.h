#pragma once

#include <Arduino.h>

// PN532 bus: 1 = SPI, 0 = I2C. See docs/wiring.md.
#ifndef PN532_USE_SPI
#if defined(ESP8266)
#define PN532_USE_SPI 1
#else
#define PN532_USE_SPI 0
#endif
#endif

#if defined(ESP8266)
#if PN532_USE_SPI
// NodeMCU hardware SPI: SCK=D5/GPIO14, MISO=D6/GPIO12, MOSI=D7/GPIO13.
// SS is on D1/GPIO5 (D8/GPIO15 must stay low at boot).
#define PN532_SS_PIN 5
#define PN532_SCK_PIN 14
#define PN532_MISO_PIN 12
#define PN532_MOSI_PIN 13
#else
// I2C: D2=GPIO4, D1=GPIO5. Avoid GPIO0/2/15 (boot straps), GPIO16 and GPIO6-11.
#define PN532_SDA_PIN 4
#define PN532_SCL_PIN 5
#define PN532_IRQ_PIN 14
#define PN532_RSTO_PIN 13
#endif
#else
// ESP32-C3 SuperMini (I2C default; SPI uses the C3 FSPI pins).
#if PN532_USE_SPI
#define PN532_SS_PIN 7
#else
#define PN532_SDA_PIN 4
#define PN532_SCL_PIN 5
#define PN532_IRQ_PIN 6
#define PN532_RSTO_PIN 7
#endif
#endif

#define PN532_I2C_HZ 100000

#define SERIAL_BAUD 115200

#define APP_VERSION "0.1.0"

// A tag is reported removed after it has been absent this long.
#define TAG_REMOVE_TIMEOUT_MS 1200
// Poll interval between passive target reads.
#define TAG_POLL_INTERVAL_MS 150
