#include "nfc/TagWriter.h"

#include <algorithm>

namespace {

constexpr size_t kType2UserOffset = 16;
constexpr size_t kClassicBlockSize = 16;
constexpr size_t kClassicBlocks = 64;
constexpr uint8_t kClassicBlocksPerSector = 4;
constexpr uint8_t kClassicSectors = 16;
constexpr size_t kMaxMismatchReports = 20;
constexpr size_t kMaxWriteRetries = 2;

const uint8_t kDefaultKey[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool isTrailerBlock(size_t block) { return block % kClassicBlocksPerSector == 3; }

String hexOf(const uint8_t* data, size_t length) {
  String out;
  char buf[3];
  for (size_t i = 0; i < length; ++i) {
    snprintf(buf, sizeof(buf), "%02X", data[i]);
    out += buf;
  }
  return out;
}

void addMismatch(WriteResult& result, const String& text) {
  if (result.mismatches.size() < kMaxMismatchReports) {
    result.mismatches.push_back(text);
  }
}

void addNote(WriteResult& result, const String& text) {
  if (result.notes.size() < kMaxMismatchReports) {
    result.notes.push_back(text);
  }
}

bool recordsEqual(const std::vector<NdefRecord>& a,
                  const std::vector<NdefRecord>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].type != b[i].type || a[i].value != b[i].value) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool TagWriter::writeRecords(const TagSnapshot& target,
                             const std::vector<NdefRecord>& records,
                             WriteResult& result) {
  result = WriteResult();
  if (target.kind != TagKind::Type2) {
    result.code = "ERR_UNSUPPORTED";
    result.message =
        "NDEF writing is implemented for Type 2 tags; use clone for Mifare "
        "Classic";
    return false;
  }

  std::vector<uint8_t> message;
  if (!ndef::encodeMessage(records, message)) {
    result.code = "ERR_NDEF";
    result.message = "could not encode the NDEF message";
    return false;
  }
  std::vector<uint8_t> tlv;
  if (!ndef::wrapTlv(message, tlv)) {
    result.code = "ERR_NDEF";
    result.message = "could not wrap the NDEF message in a TLV";
    return false;
  }

  const size_t userBytes =
      target.totalBytes > kType2UserOffset ? target.totalBytes - kType2UserOffset
                                           : 0;
  if (tlv.size() > userBytes) {
    result.code = "ERR_TARGET_TOO_SMALL";
    result.message = String("record needs ") + tlv.size() + " bytes, tag has " +
                     userBytes;
    return false;
  }

  if (!writeType2Pages(tlv, kType2UserOffset, result)) {
    return false;
  }

  std::vector<uint8_t> written = tlv;
  while (written.size() % 4 != 0) {
    written.push_back(0x00);
  }

  uint8_t buffer[16] = {};
  result.fieldsTotal = (written.size() + 15) / 16;
  result.fieldsMatched = 0;
  for (size_t offset = 0; offset < written.size(); offset += 16) {
    const uint8_t page = static_cast<uint8_t>((kType2UserOffset + offset) / 4);
    const size_t count = std::min<size_t>(16, written.size() - offset);
    if (!_reader.readPages(page, buffer) ||
        memcmp(buffer, written.data() + offset, count) != 0) {
      addMismatch(result, String("page ") + page);
      continue;
    }
    ++result.fieldsMatched;
  }

  if (result.fieldsMatched != result.fieldsTotal) {
    result.code = "ERR_VERIFY";
    result.message = "read-back verification failed";
    return false;
  }

  result.ok = true;
  result.message = "NDEF written and verified";
  return true;
}

bool TagWriter::writeType2Pages(const std::vector<uint8_t>& pages,
                                uint16_t userOffset, WriteResult& result) {
  std::vector<uint8_t> data = pages;
  while (data.size() % 4 != 0) {
    data.push_back(0x00);
  }
  if (userOffset % 4 != 0) {
    result.code = "ERR_INTERNAL";
    result.message = "write offset is not page aligned";
    return false;
  }

  for (size_t offset = 0; offset < data.size(); offset += 4) {
    const uint8_t page =
        static_cast<uint8_t>((userOffset + offset) / 4);
    bool written = false;
    for (size_t attempt = 0; attempt < kMaxWriteRetries && !written; ++attempt) {
      written = _reader.writePage(page, data.data() + offset);
    }
    if (!written) {
      result.code = "ERR_WRITE_FAILED";
      result.message = String("write failed at page ") + page;
      return false;
    }
    result.bytesWritten += 4;
    yield();
  }
  return true;
}

bool TagWriter::clone(const TagSnapshot& source, const TagSnapshot& target,
                      const CloneOptions& options, WriteResult& result) {
  result = WriteResult();
  if (source.kind == TagKind::Unknown || target.kind == TagKind::Unknown) {
    result.code = "ERR_UNKNOWN_TAG";
    result.message = "source or target tag was not identified";
    return false;
  }
  if (source.kind != target.kind) {
    result.code = "ERR_TYPE_MISMATCH";
    result.message = String("source is ") + source.kindName() + ", target is " +
                     target.kindName();
    return false;
  }
  if (!source.complete) {
    result.code = "ERR_SOURCE_INCOMPLETE";
    result.message = String("source dump is incomplete: ") + source.error;
    return false;
  }

  if (source.kind == TagKind::Type2) {
    if (target.totalBytes < source.totalBytes) {
      result.code = "ERR_TARGET_TOO_SMALL";
      result.message = String("source needs ") + source.totalBytes +
                       " bytes, target has " + target.totalBytes;
      return false;
    }

    const std::vector<uint8_t> userMemory(
        source.memory.begin() + kType2UserOffset, source.memory.end());
    if (!writeType2Pages(userMemory, kType2UserOffset, result)) {
      return false;
    }

    if (options.writeUid) {
      result.uidAttempted = true;
      if (writeUidType2(source, target, result)) {
        result.uidMatch = true;
      } else {
        result.uidLocked = true;
        addNote(result,
                "target manufacturer pages are read-only; UID was not changed");
      }
    }

    const TagUid expectedUid = options.writeUid && result.uidMatch
                                   ? source.uid
                                   : target.uid;
    if (!verifyType2(source, expectedUid, result)) {
      return false;
    }
  } else {
    if (!writeClassic(source, target.uid, options, result)) {
      return false;
    }
    if (options.writeUid) {
      result.uidAttempted = true;
      if (writeUidClassic(source, target.uid, result)) {
        result.uidMatch = true;
      } else {
        result.uidLocked = true;
        addNote(result,
                "target block 0 is read-only (Gen1a magic cards need backdoor "
                "commands the PN532 does not expose)");
      }
    }

    const TagUid expectedUid = options.writeUid && result.uidMatch
                                   ? source.uid
                                   : target.uid;
    if (!verifyClassic(source, expectedUid, options, result)) {
      return false;
    }
  }

  if (result.uidAttempted && result.uidLocked) {
    result.code = "ERR_UID_LOCKED";
    result.message =
        "data is cloned and verified, but the target UID is read-only (not a "
        "UID-writable blank)";
    return false;
  }
  if (!result.recordsMatch) {
    result.code = "ERR_VERIFY";
    result.message = "NDEF records differ after the clone";
    return false;
  }

  result.ok = true;
  result.message = options.writeUid ? "clone verified (UID + data)"
                                    : "data clone verified";
  return true;
}

bool TagWriter::writeClassic(const TagSnapshot& source,
                             const TagUid& targetUid,
                             const CloneOptions& options, WriteResult& result) {
  for (uint8_t sector = 0; sector < kClassicSectors; ++sector) {
    const uint8_t firstBlock = sector * kClassicBlocksPerSector;
    const bool keyA = _reader.authenticateBlock(
        targetUid, firstBlock, MIFARE_CMD_AUTH_A, kDefaultKey);
    const bool authenticated =
        keyA || _reader.authenticateBlock(targetUid, firstBlock,
                                          MIFARE_CMD_AUTH_B, kDefaultKey);
    if (!authenticated) {
      result.code = "ERR_AUTH_FAILED";
      result.message = String("target sector ") + sector +
                       " does not accept the default key";
      return false;
    }

    for (uint8_t block = firstBlock + 1;
         block < firstBlock + kClassicBlocksPerSector; ++block) {
      if (isTrailerBlock(block)) {
        if (!options.includeTrailers) {
          addNote(result, String("sector ") + sector + " trailer skipped");
          continue;
        }
        if (!keyA) {
          addNote(result, String("sector ") + sector +
                              " trailer skipped (KeyA unavailable)");
          continue;
        }
      }
      const uint8_t* data = source.memory.data() + static_cast<size_t>(block) * 16;
      bool written = false;
      for (size_t attempt = 0; attempt < kMaxWriteRetries && !written;
           ++attempt) {
        written = _reader.writeBlock(block, data);
      }
      if (!written) {
        result.code = "ERR_WRITE_FAILED";
        result.message = String("write failed at block ") + block;
        return false;
      }
      result.bytesWritten += kClassicBlockSize;
      yield();
    }
  }
  return true;
}

bool TagWriter::writeUidType2(const TagSnapshot& source,
                              const TagSnapshot& target, WriteResult& result) {
  // Pages 0-2 hold UID0-2/BCC0, UID3-6 and BCC1/internal/lock bytes. A
  // UID-writable clone needs all three, otherwise BCC1 no longer matches the
  // new UID and the tag stops answering.
  bool wroteAny = false;
  for (uint8_t page = 0; page < 3; ++page) {
    const uint8_t* data = source.memory.data() + static_cast<size_t>(page) * 4;
    bool written = false;
    for (size_t attempt = 0; attempt < kMaxWriteRetries && !written; ++attempt) {
      written = _reader.writePage(page, data);
    }
    if (!written) {
      if (wroteAny) {
        restoreType2Header(target, result);
      }
      return false;
    }
    wroteAny = true;
  }

  if (source.memory[10] != 0 || source.memory[11] != 0) {
    addNote(result,
            "source static lock bits were copied; further writes may be "
            "restricted");
  }

  TagUid uid;
  if (!reselect(uid)) {
    return false;
  }
  uint8_t header[16] = {};
  if (!_reader.readPages(0, header)) {
    return false;
  }
  return memcmp(header, source.memory.data(), source.uid.length) == 0;
}

void TagWriter::restoreType2Header(const TagSnapshot& target,
                                   WriteResult& result) {
  for (uint8_t page = 0; page < 3; ++page) {
    const uint8_t* data = target.memory.data() + static_cast<size_t>(page) * 4;
    if (!_reader.writePage(page, data)) {
      addNote(result,
              "could not restore the original manufacturer pages; the target "
              "may need re-initialising");
      return;
    }
  }
  addNote(result, "original UID pages restored");
}

bool TagWriter::writeUidClassic(const TagSnapshot& source,
                                const TagUid& targetUid, WriteResult& result) {
  if (!_reader.authenticateBlock(targetUid, 0, MIFARE_CMD_AUTH_A,
                                 kDefaultKey) &&
      !_reader.authenticateBlock(targetUid, 0, MIFARE_CMD_AUTH_B,
                                 kDefaultKey)) {
    return false;
  }
  bool written = false;
  for (size_t attempt = 0; attempt < kMaxWriteRetries && !written; ++attempt) {
    written = _reader.writeBlock(0, source.memory.data());
  }
  if (!written) {
    return false;
  }

  TagUid uid;
  if (!reselect(uid)) {
    return false;
  }
  if (!_reader.authenticateBlock(uid, 0, MIFARE_CMD_AUTH_A, kDefaultKey) &&
      !_reader.authenticateBlock(uid, 0, MIFARE_CMD_AUTH_B, kDefaultKey)) {
    return false;
  }
  uint8_t block[16] = {};
  if (!_reader.readBlock(0, block)) {
    return false;
  }
  return memcmp(block, source.memory.data(), source.uid.length) == 0;
}

bool TagWriter::verifyType2(const TagSnapshot& source,
                            const TagUid& expectedUid, WriteResult& result) {
  TagUid uid;
  if (!reselect(uid)) {
    result.code = "ERR_VERIFY";
    result.message = "tag not detected after writing";
    return false;
  }

  TagSnapshot after;
  if (!_tagReader.read(uid, after) || after.kind != TagKind::Type2) {
    result.code = "ERR_VERIFY";
    result.message = "could not re-read the tag after writing";
    return false;
  }

  const size_t compareBytes =
      std::min(source.memory.size(), after.memory.size());
  result.fieldsTotal = compareBytes > kType2UserOffset
                           ? (compareBytes - kType2UserOffset) / 4
                           : 0;
  result.fieldsMatched = 0;
  for (size_t offset = kType2UserOffset; offset + 4 <= compareBytes;
       offset += 4) {
    if (memcmp(source.memory.data() + offset, after.memory.data() + offset,
               4) == 0) {
      ++result.fieldsMatched;
    } else {
      addMismatch(result, String("page ") + (offset / 4) + ": expected " +
                              hexOf(source.memory.data() + offset, 4) +
                              " got " + hexOf(after.memory.data() + offset, 4));
    }
  }

  result.uidMatch = after.uid.equals(expectedUid);
  result.recordsMatch = recordsEqual(source.records, after.records);
  if (result.fieldsMatched != result.fieldsTotal) {
    result.code = "ERR_VERIFY";
    result.message = "data mismatch after clone";
    return false;
  }
  return true;
}

bool TagWriter::verifyClassic(const TagSnapshot& source,
                              const TagUid& expectedUid,
                              const CloneOptions& options,
                              WriteResult& result) {
  TagUid uid;
  if (!reselect(uid)) {
    result.code = "ERR_VERIFY";
    result.message = "tag not detected after writing";
    return false;
  }

  TagSnapshot after;
  if (!_tagReader.read(uid, after) ||
      after.kind != TagKind::MifareClassic) {
    result.code = "ERR_VERIFY";
    result.message = "could not re-read the tag after writing";
    return false;
  }

  result.fieldsMatched = 0;
  result.fieldsTotal = 0;
  for (size_t block = 1; block < kClassicBlocks; ++block) {
    if (isTrailerBlock(block) && !options.includeTrailers) {
      continue;
    }
    ++result.fieldsTotal;
    const size_t offset = block * 16;
    if (offset + 16 <= after.memory.size() &&
        memcmp(source.memory.data() + offset, after.memory.data() + offset,
               16) == 0) {
      ++result.fieldsMatched;
    } else {
      addMismatch(result, String("block ") + block);
    }
  }

  result.uidMatch = after.uid.equals(expectedUid);
  result.recordsMatch = recordsEqual(source.records, after.records);
  if (result.fieldsMatched != result.fieldsTotal) {
    result.code = "ERR_VERIFY";
    result.message = "data mismatch after clone";
    return false;
  }
  return true;
}

bool TagWriter::reselect(TagUid& uid) {
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (_reader.readTarget(uid, 300)) {
      return true;
    }
    delay(50);
  }
  return false;
}
