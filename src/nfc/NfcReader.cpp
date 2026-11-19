#include "nfc/NfcReader.h"

#include "config.h"

// Adafruit_PN532 parses ATQA/SAK out of its InListPassiveTarget response but
// does not return them; the response stays in this global buffer until the
// next command.
extern uint8_t pn532_packetbuffer[];

namespace {

constexpr uint8_t kListAtqaOffset = 9;
constexpr uint8_t kListSakOffset = 11;

constexpr uint8_t kPassiveActivationRetries = 1;

#if !PN532_USE_SPI
constexpr uint32_t kClockStretchLimitUs = 20000;

void capClockStretch() {
#if defined(ESP8266)
  // The ESP8266 core resets this to 150 ms on every Wire.begin(). A confused
  // PN532 can stretch every bit, which makes Adafruit's waitready() take
  // minutes and starves the rest of the firmware.
  Wire.setClockStretchLimit(kClockStretchLimitUs);
#endif
}

void beginI2c(uint8_t sda, uint8_t scl) {
#if defined(ESP8266)
  Wire.begin(sda, scl);
#else
  Wire.begin(sda, scl, PN532_I2C_HZ);
#endif
  Wire.setClock(PN532_I2C_HZ);
  capClockStretch();
}

uint8_t scanBus(const char* label) {
  Serial.printf("[nfc] %s scan:", label);
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", address);
      ++found;
    }
  }
  if (found == 0) {
    Serial.print(" no devices");
  }
  Serial.printf(" (%u found)\n", found);
  return found;
}

void diagnoseI2c() {
  Serial.printf("[nfc] idle SDA=%d SCL=%d (expect 1/1)\n",
                digitalRead(PN532_SDA_PIN), digitalRead(PN532_SCL_PIN));
  scanBus("as wired");

  Wire.beginTransmission(0x24);
  const uint8_t probe = Wire.endTransmission();
  Serial.printf("[nfc] probe 0x24: status %u (0=ACK, 2=address NACK)\n",
                probe);
  if (probe == 0) {
    return;
  }

#if defined(ESP8266)
  // Wire.begin() re-initialises the peripheral, so a swapped bus can be tested.
  beginI2c(PN532_SCL_PIN, PN532_SDA_PIN);
  if (scanBus("swapped SDA/SCL") > 0) {
    Serial.println("[nfc] device found with SDA/SCL swapped - swap the two wires");
  }
  beginI2c(PN532_SDA_PIN, PN532_SCL_PIN);
#endif
}
#else  // PN532_USE_SPI

// PN532 SPI status read: returns 0x01 when ready, 0xFF if MISO is floating
// (module absent/unpowered/held in reset).
uint8_t spiStatusProbe() {
  pinMode(PN532_SS_PIN, OUTPUT);
  digitalWrite(PN532_SS_PIN, HIGH);
  SPI.beginTransaction(SPISettings(1000000, LSBFIRST, SPI_MODE0));
  digitalWrite(PN532_SS_PIN, LOW);
  delayMicroseconds(10);
  SPI.transfer(PN532_SPI_STATREAD);
  const uint8_t status = SPI.transfer(0x00);
  digitalWrite(PN532_SS_PIN, HIGH);
  SPI.endTransaction();
  return status;
}

#endif  // PN532_USE_SPI

void printFirmware(uint32_t version) {
  Serial.printf("[nfc] PN532 IC 0x%02X firmware %u.%u (support 0x%02X)\n",
                static_cast<unsigned>((version >> 24) & 0xFF),
                static_cast<unsigned>((version >> 16) & 0xFF),
                static_cast<unsigned>((version >> 8) & 0xFF),
                static_cast<unsigned>(version & 0xFF));
}

}  // namespace

bool NfcReader::begin() {
  _ready = false;

#if PN532_USE_SPI
  pinMode(PN532_SS_PIN, OUTPUT);
  digitalWrite(PN532_SS_PIN, HIGH);
  Serial.println(
      "[nfc] bus: SPI (SCK=D5/14, MISO=D6/12, MOSI=D7/13, SS=D1/5)");
  if (!_pn532.begin()) {
    Serial.println("[nfc] SPI bus init failed");
    return false;
  }
  delay(100);
#else
  beginI2c(PN532_SDA_PIN, PN532_SCL_PIN);
  pinMode(PN532_IRQ_PIN, INPUT_PULLUP);
  pinMode(PN532_RSTO_PIN, OUTPUT);

  // Adafruit_PN532::begin() cannot detect the address while the chip is in
  // reset, so the reset is issued first.
  _pn532.begin();  // calls Wire.begin() again and resets the stretch limit
  capClockStretch();
  _pn532.reset();
  delay(300);
#endif

  Serial.printf("[nfc] probing PN532 over %s...\n", busName());
  const uint32_t probeStart = millis();
  _firmware = _pn532.getFirmwareVersion();
  Serial.printf("[nfc] probe finished in %lu ms\n",
                static_cast<unsigned long>(millis() - probeStart));
  if (_firmware == 0) {
    Serial.println("[nfc] no response from PN532");
#if PN532_USE_SPI
    Serial.printf(
        "[nfc] SPI status byte 0x%02X (0x01=ready, 0xFF=MISO floating)\n",
        spiStatusProbe());
    Serial.println(
        "[nfc] SPI checks: DIP SW1=OFF SW2=ON, SCK=D5, MISO=D6, MOSI=D7, "
        "SS=D1, RSTO=3V3, VCC=3V3, common GND, power-cycle after DIP change");
#else
    diagnoseI2c();
#endif
    return false;
  }
  printFirmware(_firmware);

  if (!_pn532.SAMConfig()) {
    Serial.println("[nfc] SAMConfiguration failed");
    return false;
  }
  if (!_pn532.setPassiveActivationRetries(kPassiveActivationRetries)) {
    Serial.println("[nfc] RFConfiguration failed");
    return false;
  }

  _reads = 0;
  _misses = 0;
  _busErrors = 0;
  _ready = true;
  Serial.printf("[nfc] ready (ISO14443A, %s)\n", busName());
  return true;
}

bool NfcReader::healthCheck() {
  if (!_ready) {
    return false;
  }
  if (_pn532.getFirmwareVersion() == _firmware) {
    return true;
  }
  Serial.println("[nfc] health check failed, marking reader offline");
  _ready = false;
  return false;
}

bool NfcReader::readTarget(TagUid& uid, uint16_t timeoutMs) {
  uid.clear();
  if (!_ready) {
    return false;
  }

  uint8_t buffer[TagUid::kMaxLength] = {};
  uint8_t length = 0;
  if (_pn532.readPassiveTargetID(PN532_MIFARE_ISO14443A, buffer, &length,
                                 timeoutMs) &&
      length > 0) {
    if (length > TagUid::kMaxLength) {
      length = TagUid::kMaxLength;
    }
    memcpy(uid.bytes, buffer, length);
    uid.length = length;
    uid.atqa = static_cast<uint16_t>(
        (pn532_packetbuffer[kListAtqaOffset] << 8) |
        pn532_packetbuffer[kListAtqaOffset + 1]);
    uid.sak = pn532_packetbuffer[kListSakOffset];
    ++_reads;
    return true;
  }

  ++_misses;
  return false;
}

bool NfcReader::transceive(const uint8_t* send, uint8_t sendLength,
                           uint8_t* response, uint8_t& responseLength) {
  if (!_ready) {
    return false;
  }
  uint8_t buffer[64] = {};
  uint8_t length = sizeof(buffer);
  if (!_pn532.inDataExchange(const_cast<uint8_t*>(send), sendLength, buffer,
                             &length)) {
    ++_busErrors;
    return false;
  }
  if (length > responseLength) {
    length = responseLength;
  }
  memcpy(response, buffer, length);
  responseLength = length;
  return true;
}

bool NfcReader::readPages(uint8_t page, uint8_t out[16]) {
  if (!_ready) {
    return false;
  }
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t current = static_cast<uint8_t>(page + i);
    const uint8_t ok =
        current < 4 ? _pn532.mifareultralight_ReadPage(current, out + i * 4)
                    : _pn532.ntag2xx_ReadPage(current, out + i * 4);
    if (ok == 0) {
      ++_busErrors;
      return false;
    }
  }
  return true;
}

bool NfcReader::writePage(uint8_t page, const uint8_t in[4]) {
  if (!_ready) {
    return false;
  }
  const uint8_t status =
      page < 4
          ? _pn532.mifareultralight_WritePage(page,
                                              const_cast<uint8_t*>(in))
          : _pn532.ntag2xx_WritePage(page, const_cast<uint8_t*>(in));
  if (status == 0) {
    ++_busErrors;
  }
  return status != 0;
}

bool NfcReader::authenticateBlock(const TagUid& uid, uint8_t block,
                                  uint8_t keyType, const uint8_t key[6]) {
  if (!_ready) {
    return false;
  }
  const uint8_t status = _pn532.mifareclassic_AuthenticateBlock(
      const_cast<uint8_t*>(uid.bytes), uid.length, block, keyType,
      const_cast<uint8_t*>(key));
  if (status == 0) {
    ++_busErrors;
  }
  return status != 0;
}

bool NfcReader::readBlock(uint8_t block, uint8_t out[16]) {
  if (!_ready) {
    return false;
  }
  const uint8_t status = _pn532.mifareclassic_ReadDataBlock(block, out);
  if (status == 0) {
    ++_busErrors;
  }
  return status != 0;
}

bool NfcReader::writeBlock(uint8_t block, const uint8_t in[16]) {
  if (!_ready) {
    return false;
  }
  const uint8_t status =
      _pn532.mifareclassic_WriteDataBlock(block, const_cast<uint8_t*>(in));
  if (status == 0) {
    ++_busErrors;
  }
  return status != 0;
}
