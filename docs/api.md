# HTTP + WebSocket API

Base URL: `http://<board-ip>/` (or `http://pn532.local/` on the same LAN).
WebSocket: `ws://<board-ip>:81/`.

All HTTP responses are JSON. Device-level failures return `{"ok": false, "code", "message"}`
with HTTP 200/409; malformed requests return HTTP 400.

## Endpoints

### `GET /api/status`

```json
{
  "event": "status",
  "ok": true,
  "version": "0.1.0",
  "firmware": "1.6",
  "readerReady": true,
  "present": true,
  "busy": false,
  "uid": "04 A1 B2 C3 D4 E5 80",
  "hasSource": true,
  "sourceUid": "04 A1 B2 C3 D4 E5 80",
  "historyCount": 3,
  "scans": 12,
  "reads": 48,
  "misses": 940,
  "busErrors": 0,
  "mode": "STA",
  "ip": "192.168.1.42",
  "ssid": "HomeWiFi",
  "rssi": -55,
  "ap": false,
  "uptimeMs": 154321,
  "heap": 260112
}
```

### `GET /api/tag`

```json
{
  "event": "tag",
  "ok": true,
  "present": true,
  "tag": {
    "uid": "04 A1 B2 C3 D4 E5 80",
    "uidCompact": "04A1B2C3D4E580",
    "uidLength": 7,
    "atqa": "0044",
    "sak": "00",
    "kind": "Type 2",
    "model": "NTAG215",
    "capacity": 496,
    "totalBytes": 512,
    "complete": true,
    "error": "",
    "ndef": true,
    "chunkSize": 4,
    "memory": "04A1B2C3...E1103E00...",
    "readAgeMs": 1200,
    "records": [
      { "type": "text", "value": "hello tag", "language": "en", "utf16": false },
      { "type": "uri", "value": "https://example.com" }
    ]
  }
}
```

`chunkSize` is 4 (pages) for Type 2 and 16 (blocks) for Mifare Classic.
`/api/status` also reports `"bus": "SPI"` and `"armed"`.
`memory` is the full dump as uppercase hex.

### `GET /api/result`

Returns the last write/clone result (same shape as the WebSocket `result`
event), or `ERR_NO_RESULT` if nothing has run yet.

### `GET /api/history`

The last 8 tags, newest first:

```json
{
  "ok": true,
  "items": [
    { "uid": "04 A1 ...", "uidCompact": "04A1...", "model": "NTAG215",
      "kind": "Type 2", "capacity": 496, "records": 2, "ageMs": 8400 }
  ]
}
```

### `POST /api/source`

Select the clone source.

```json
{}
```

uses the tag currently on the reader. To pick a previous tag:

```json
{ "uid": "04A1B2C3D4E580" }
```

Response: `{"ok": true, "sourceUid": "04 A1 ...", "message": "clone source selected"}`.

### `POST /api/read`

Re-reads the tag on the reader and returns the same shape as `GET /api/tag`.
Body can be empty.

### `POST /api/write`

Writes a new NDEF message to the current Type 2 tag (Text and URI records).

```json
{
  "records": [
    { "type": "text", "value": "hello", "language": "en" },
    { "type": "uri", "value": "https://example.com" }
  ]
}
```

If no tag is on the reader the clone is **armed** instead: the response has
`"armed": true` and the clone runs as soon as the next tag is detected. The
result is pushed on the WebSocket (`event: "result"`) and stored for
`GET /api/result`.

Response is a [write result](#write-result). Mifare Classic targets return
`ERR_UNSUPPORTED` — use clone instead.

### `POST /api/clone`

Writes the selected source snapshot to the tag currently on the reader.

```json
{ "writeUid": true, "includeTrailers": false }
```

- `writeUid` — also write pages 0–2 (Type 2) or block 0 (Mifare Classic).
  Genuine tags reject this and the data is still cloned.
- `includeTrailers` — Mifare Classic sector trailers are skipped by default;
  writing them can change keys and lock out sectors.

Response is a [write result](#write-result).

### `POST /api/wifi`

Stores credentials in NVS and reboots after replying.

```json
{ "ssid": "HomeWiFi", "password": "secret" }
```

### `GET /api/wifi/scan`

```json
{
  "ok": true,
  "networks": [ { "ssid": "HomeWiFi", "rssi": -55, "encryption": 3 } ]
}
```

## Write result

```json
{
  "event": "result",
  "ok": true,
  "code": "",
  "message": "clone verified (UID + data)",
  "bytesWritten": 496,
  "fieldsMatched": 124,
  "fieldsTotal": 124,
  "uidAttempted": true,
  "uidLocked": false,
  "uidMatch": true,
  "recordsMatch": true,
  "mismatches": [],
  "notes": ["sector 15 trailer skipped"]
}
```

| Code | Meaning |
|---|---|
| `ERR_NO_SOURCE` | no clone source selected |
| `ERR_NO_TAG` | no tag on the reader (clone arms instead of failing) |
| `ERR_SOURCE_INCOMPLETE` | source dump has unreadable sectors/pages |
| `ERR_TYPE_MISMATCH` | source and target are different tag families |
| `ERR_TARGET_TOO_SMALL` | target cannot hold the source data |
| `ERR_AUTH_FAILED` | Mifare sector does not accept the default key |
| `ERR_WRITE_FAILED` | a page/block write was NAKed |
| `ERR_VERIFY` | read-back did not match the source |
| `ERR_UID_LOCKED` | data cloned, but the target UID is read-only |
| `ERR_SAME_TAG` | source and target have identical UID and contents |
| `ERR_UNSUPPORTED` | operation not implemented for that tag family |
| `ERR_NDEF` | record could not be encoded |

## WebSocket events

Connect to `ws://<ip>:81/`. On connect the server sends the current status, the
current tag and the log history, then streams:

| `event` | Payload |
|---|---|
| `status` | same object as `GET /api/status` |
| `tag` | `present` + `tag` object |
| `removed` | `uid` |
| `log` | `message` |
| `result` | write result after `/api/write` or `/api/clone` |
| `pong` | reply to a client `ping` |

Send the text frame `ping` every ~25 s to detect dead connections.
