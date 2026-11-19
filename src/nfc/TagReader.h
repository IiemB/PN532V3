#pragma once

#include "nfc/NfcReader.h"
#include "nfc/TagSnapshot.h"

class TagReader {
 public:
  explicit TagReader(NfcReader& reader) : _reader(reader) {}

  // Dumps the currently selected tag. Returns false only when the tag cannot be
  // classified at all; partial dumps return true with complete == false.
  bool read(const TagUid& uid, TagSnapshot& snapshot);

 private:
  bool readType2(TagSnapshot& snapshot);
  bool readClassic(TagSnapshot& snapshot);
  void readVersionInfo(TagSnapshot& snapshot);
  void parseRecords(TagSnapshot& snapshot, size_t offset);
  bool parseRecordsAtAny(TagSnapshot& snapshot,
                         const size_t* offsets, size_t count);

  NfcReader& _reader;
};
