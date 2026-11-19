#include "nfc/TagReader.h"

#include <algorithm>

namespace {

constexpr uint8_t kType2CcMagic = 0xE1;
constexpr uint8_t kType2CcVersion = 0x10;
constexpr size_t kType2UserOffset = 16;   // page 4
constexpr size_t kClassicUserOffset = 64;  // block 4 (sector 1)
constexpr uint8_t kClassicSectors = 16;
constexpr uint8_t kClassicBlocksPerSector = 4;

const uint8_t kDefaultKey[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct VersionEntry {
  uint8_t storage;
  const char* model;
};

const VersionEntry kVersionModels[] = {
    {0x0B, "Mifare Ultralight (144 B)"},
    {0x0E, "NTAG213"},
    {0x0F, "NTAG215"},
    {0x11, "NTAG216"},
};

String modelForCapacity(uint16_t capacity) {
  switch (capacity) {
    case 144:
      return "NTAG213 / Ultralight";
    case 496:
      return "NTAG215";
    case 872:
      return "NTAG216";
    default:
      return String("Type 2 (") + capacity + " B)";
  }
}

}  // namespace

bool TagReader::read(const TagUid& uid, TagSnapshot& snapshot) {
  snapshot.reset();
  snapshot.uid = uid;
  snapshot.readAtMs = millis();

  uint8_t first[16] = {};
  if (_reader.readPages(0, first)) {
    if (first[12] == kType2CcMagic && first[13] == kType2CcVersion) {
      return readType2(snapshot);
    }
    snapshot.kind = TagKind::Type2;
    snapshot.model = "Type 2 (unformatted)";
    snapshot.memory.assign(first, first + sizeof(first));
    snapshot.totalBytes = sizeof(first);
    snapshot.complete = false;
    snapshot.error = "no NDEF capability container on page 3";
    return true;
  }

  // The failed Type 2 READ above is NAKed by Classic cards and can leave the
  // PICC session in an error state; re-list the target before authenticating.
  TagUid refreshed;
  if (_reader.readTarget(refreshed) && !refreshed.equals(uid)) {
    snapshot.error = "tag changed during identification";
    return false;
  }

  if (_reader.authenticateBlock(uid, 0, MIFARE_CMD_AUTH_A, kDefaultKey) ||
      _reader.authenticateBlock(uid, 0, MIFARE_CMD_AUTH_B, kDefaultKey)) {
    return readClassic(snapshot);
  }

  snapshot.error = "not a Type 2 tag and Mifare authentication failed";
  return false;
}

bool TagReader::readType2(TagSnapshot& snapshot) {
  uint8_t header[16] = {};
  if (!_reader.readPages(0, header)) {
    snapshot.error = "failed to read page 0";
    return false;
  }

  const uint16_t capacity = static_cast<uint16_t>(header[14]) * 8;
  if (capacity < 16 || capacity > 888) {
    snapshot.error = String("invalid CC size byte 0x") + String(header[14], HEX);
    return false;
  }

  const uint16_t pages = static_cast<uint16_t>(capacity / 4 + 4);
  snapshot.kind = TagKind::Type2;
  snapshot.capacity = capacity;
  snapshot.totalBytes = static_cast<uint16_t>(pages * 4);
  snapshot.memory.assign(snapshot.totalBytes, 0);
  memcpy(snapshot.memory.data(), header, sizeof(header));

  for (uint16_t page = 4; page < pages; page += 4) {
    uint8_t buffer[16] = {};
    const uint16_t offset = static_cast<uint16_t>(page * 4);
    const uint16_t count =
        std::min<uint16_t>(sizeof(buffer), snapshot.totalBytes - offset);
    if (!_reader.readPages(static_cast<uint8_t>(page), buffer)) {
      snapshot.complete = false;
      snapshot.error = String("read failed at page ") + page;
      break;
    }
    memcpy(snapshot.memory.data() + offset, buffer, count);
    yield();
  }

  snapshot.model = modelForCapacity(capacity);
  if (snapshot.complete) {
    readVersionInfo(snapshot);
  }
  parseRecords(snapshot, kType2UserOffset);
  return true;
}

bool TagReader::readClassic(TagSnapshot& snapshot) {
  snapshot.kind = TagKind::MifareClassic;
  snapshot.capacity = 1024;
  snapshot.totalBytes = kClassicSectors * kClassicBlocksPerSector * 16;
  snapshot.memory.assign(snapshot.totalBytes, 0);

  uint8_t readableSectors = 0;
  uint8_t blocksRead = 0;
  for (uint8_t sector = 0; sector < kClassicSectors; ++sector) {
    const uint8_t firstBlock = sector * kClassicBlocksPerSector;
    bool authenticated =
        _reader.authenticateBlock(snapshot.uid, firstBlock, MIFARE_CMD_AUTH_A,
                                  kDefaultKey) ||
        _reader.authenticateBlock(snapshot.uid, firstBlock, MIFARE_CMD_AUTH_B,
                                  kDefaultKey);
    if (!authenticated) {
      TagUid refreshed;
      if (_reader.readTarget(refreshed) && refreshed.equals(snapshot.uid)) {
        authenticated = _reader.authenticateBlock(
                            snapshot.uid, firstBlock, MIFARE_CMD_AUTH_A,
                            kDefaultKey) ||
                        _reader.authenticateBlock(
                            snapshot.uid, firstBlock, MIFARE_CMD_AUTH_B,
                            kDefaultKey);
      }
    }
    if (!authenticated) {
      snapshot.complete = false;
      if (snapshot.error.isEmpty()) {
        snapshot.error = String("authentication failed for sector ") + sector;
      }
      continue;
    }

    ++readableSectors;
    for (uint8_t block = firstBlock; block < firstBlock + kClassicBlocksPerSector;
         ++block) {
      uint8_t buffer[16] = {};
      bool read = _reader.readBlock(block, buffer);
      if (!read) {
        // Re-authenticate and retry once; Classic cards sometimes NAK the
        // first READ after the Type 2 probe.
        _reader.authenticateBlock(snapshot.uid, firstBlock, MIFARE_CMD_AUTH_A,
                                  kDefaultKey);
        read = _reader.readBlock(block, buffer);
      }
      if (!read) {
        snapshot.complete = false;
        if (snapshot.error.isEmpty()) {
          snapshot.error = String("read failed at block ") + block;
        }
        continue;
      }
      ++blocksRead;
      memcpy(snapshot.memory.data() + static_cast<size_t>(block) * 16, buffer,
             sizeof(buffer));
      yield();
    }
  }

  Serial.printf("[tag] classic: %u/16 sectors auth, %u/64 blocks read\n",
                readableSectors, blocksRead);
  if (readableSectors == 0) {
    snapshot.error = "no sector could be authenticated with the default key";
    return false;
  }

  snapshot.model = "Mifare Classic 1K";
  const size_t offsets[] = {kClassicUserOffset, 16};
  parseRecordsAtAny(snapshot, offsets, 2);
  return true;
}

void TagReader::readVersionInfo(TagSnapshot& snapshot) {
  uint8_t command[1] = {0x60};  // GET_VERSION
  uint8_t response[16] = {};
  uint8_t length = sizeof(response);
  if (!_reader.transceive(command, sizeof(command), response, length) ||
      length < 8) {
    return;
  }
  const uint8_t storage = response[6];
  for (const VersionEntry& entry : kVersionModels) {
    if (entry.storage == storage) {
      snapshot.model = entry.model;
      return;
    }
  }
}

void TagReader::parseRecords(TagSnapshot& snapshot, size_t offset) {
  if (offset >= snapshot.memory.size()) {
    return;
  }
  std::vector<NdefRecord> records;
  if (ndef::parseTlv(snapshot.memory.data() + offset,
                     snapshot.memory.size() - offset, records)) {
    snapshot.records = std::move(records);
    snapshot.ndefFound = true;
  }
}

bool TagReader::parseRecordsAtAny(TagSnapshot& snapshot, const size_t* offsets,
                                  size_t count) {
  for (size_t i = 0; i < count; ++i) {
    parseRecords(snapshot, offsets[i]);
    if (snapshot.ndefFound) {
      return true;
    }
  }
  return false;
}
