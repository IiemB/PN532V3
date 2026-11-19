#pragma once

#include <functional>
#include <vector>

#include "app/Scanner.h"
#include "nfc/NfcReader.h"
#include "nfc/TagReader.h"
#include "nfc/TagSnapshot.h"
#include "nfc/TagWriter.h"

class TagService {
 public:
  using SnapshotCallback = std::function<void(const TagSnapshot&)>;
  using RemovedCallback = std::function<void(const TagUid&)>;
  using LogCallback = std::function<void(const String&)>;
  using ResultCallback = std::function<void(const WriteResult&)>;

  void begin();
  void loop();

  bool setSourceCurrent(String& error);
  bool setSourceFromUid(const String& uid, String& error);
  bool clone(bool writeUid, bool includeTrailers, WriteResult& result);
  bool writeRecords(const std::vector<NdefRecord>& records,
                    WriteResult& result);
  bool reread(String& error);

  const TagSnapshot& snapshot() const { return _snapshot; }
  const TagSnapshot& source() const { return _source; }
  const std::vector<TagSnapshot>& history() const { return _history; }
  bool hasSource() const { return _hasSource; }
  bool armed() const { return _armed; }
  bool hasResult() const { return _hasResult; }
  const WriteResult& lastResult() const { return _lastResult; }
  bool present() const { return _present; }
  bool busy() const { return _busy; }
  bool readerReady() const { return _nfc.isReady(); }
  const NfcReader& reader() const { return _nfc; }
  uint32_t scans() const { return _scanner.detections(); }

  void setSnapshotCallback(SnapshotCallback callback) {
    _onSnapshot = std::move(callback);
  }
  void setRemovedCallback(RemovedCallback callback) {
    _onRemoved = std::move(callback);
  }
  void setLogCallback(LogCallback callback) { _onLog = std::move(callback); }
  void setResultCallback(ResultCallback callback) {
    _onResult = std::move(callback);
  }

 private:
  void retryInit();
  void onTagDetected(const TagUid& uid);
  void onTagRemoved(const TagUid& uid);
  bool refreshSnapshot(String& error);
  bool waitForTag(uint32_t timeoutMs);
  void rememberSnapshot(const TagSnapshot& snapshot);
  void log(const String& message);

  NfcReader _nfc;
  Scanner _scanner{_nfc};
  TagReader _tagReader{_nfc};
  TagWriter _writer{_nfc, _tagReader};

  TagSnapshot _snapshot;
  TagSnapshot _source;
  std::vector<TagSnapshot> _history;

  bool _present = false;
  bool _busy = false;
  bool _hasSource = false;
  bool _armed = false;
  bool _hasResult = false;
  CloneOptions _armedOptions;
  WriteResult _lastResult;
  uint32_t _lastInitMs = 0;
  uint32_t _lastHealthMs = 0;

  SnapshotCallback _onSnapshot;
  RemovedCallback _onRemoved;
  LogCallback _onLog;
  ResultCallback _onResult;
};
