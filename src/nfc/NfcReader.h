#pragma once

#include <Adafruit_PN532.h>

#include "config.h"

#if PN532_USE_SPI
#include <SPI.h>
#else
#include <Wire.h>
#endif

#include "nfc/TagInfo.h"

class NfcReader {
 public:
  bool begin();
  bool isReady() const { return _ready; }
  bool healthCheck();

  // Returns false when no tag is present or the bus read failed.
  bool readTarget(TagUid& uid, uint16_t timeoutMs = 1000);

  // Raw InDataExchange against the currently selected target.
  bool transceive(const uint8_t* send, uint8_t sendLength, uint8_t* response,
                  uint8_t& responseLength);

  // Type 2 (NTAG/Ultralight): one READ returns 16 bytes (4 pages).
  bool readPages(uint8_t page, uint8_t out[16]);
  bool writePage(uint8_t page, const uint8_t in[4]);

  // Mifare Classic.
  bool authenticateBlock(const TagUid& uid, uint8_t block, uint8_t keyType,
                         const uint8_t key[6]);
  bool readBlock(uint8_t block, uint8_t out[16]);
  bool writeBlock(uint8_t block, const uint8_t in[16]);

  uint32_t firmwareVersion() const { return _firmware; }
  const char* busName() const { return PN532_USE_SPI ? "SPI" : "I2C"; }
  uint32_t reads() const { return _reads; }
  uint32_t misses() const { return _misses; }
  uint32_t busErrors() const { return _busErrors; }

 private:
#if PN532_USE_SPI
  Adafruit_PN532 _pn532{PN532_SS_PIN, &SPI};
#else
  Adafruit_PN532 _pn532{PN532_IRQ_PIN, PN532_RSTO_PIN, &Wire};
#endif

  bool _ready = false;
  uint32_t _firmware = 0;
  uint32_t _reads = 0;
  uint32_t _misses = 0;
  uint32_t _busErrors = 0;
};
