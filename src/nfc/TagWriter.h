#pragma once

#include <vector>

#include "nfc/NfcReader.h"
#include "nfc/TagReader.h"
#include "nfc/TagSnapshot.h"

struct CloneOptions {
  bool writeUid = false;
  bool includeTrailers = false;
};

struct WriteResult {
  bool ok = false;
  bool armed = false;
  String code;
  String message;
  uint32_t bytesWritten = 0;
  uint32_t fieldsMatched = 0;
  uint32_t fieldsTotal = 0;
  bool uidAttempted = false;
  bool uidLocked = false;
  bool uidMatch = false;
  bool recordsMatch = true;
  std::vector<String> mismatches;
  std::vector<String> notes;
};

class TagWriter {
 public:
  TagWriter(NfcReader& reader, TagReader& tagReader)
      : _reader(reader), _tagReader(tagReader) {}

  bool writeRecords(const TagSnapshot& target,
                    const std::vector<NdefRecord>& records,
                    WriteResult& result);

  bool clone(const TagSnapshot& source, const TagSnapshot& target,
             const CloneOptions& options, WriteResult& result);

 private:
  bool writeType2Pages(const std::vector<uint8_t>& pages, uint16_t userOffset,
                       WriteResult& result);
  bool writeClassic(const TagSnapshot& source, const TagUid& targetUid,
                    const CloneOptions& options, WriteResult& result);
  bool writeUidType2(const TagSnapshot& source,
                     const TagSnapshot& target, WriteResult& result);
  bool writeUidClassic(const TagSnapshot& source, const TagUid& targetUid,
                       WriteResult& result);
  void restoreType2Header(const TagSnapshot& target, WriteResult& result);
  bool verifyType2(const TagSnapshot& source, const TagUid& expectedUid,
                   WriteResult& result);
  bool verifyClassic(const TagSnapshot& source, const TagUid& expectedUid,
                     const CloneOptions& options, WriteResult& result);
  bool reselect(TagUid& uid);

  NfcReader& _reader;
  TagReader& _tagReader;
};
