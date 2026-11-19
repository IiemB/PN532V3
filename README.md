# PN532V3 — NodeMCU / ESP32 NFC reader-writer

Elechouse PN532 V3 + **NodeMCU ESP8266** (default) or ESP32-C3 SuperMini:
reads, writes and clones 13.56 MHz tags, controlled from a browser UI served by
the board itself.

## Status

| Area | State |
|---|---|
| NodeMCU ESP-12E build (`nodemcuv2`, SPI) | **verified on hardware** — 45% RAM, 40% flash |
| ESP32-C3 SuperMini build (`esp32c3`) | kept working |
| Wiring + DIP switch documentation | [docs/wiring.md](docs/wiring.md) |
| PN532 SPI init (fw 1.6), UID scan, presence tracking | **verified on hardware** |
| Tag dump: Mifare Classic auth + 64 blocks | **verified on hardware** (Classic 1K) |
| NDEF Text/URI write with read-back verification | done |
| Clone engine with page/block diff report | **verified on hardware** (47/47 blocks, trailers skipped) |
| REST + WebSocket API | [docs/api.md](docs/api.md) |
| Embedded web UI (scan, hex view, write, clone, log, WiFi) | done |
| WiFi STA with `PN532-Setup` AP fallback, credentials in EEPROM (ESP8266) / NVS (ESP32) | done |
| NTAG/Ultralight dump + NDEF write | implemented, not yet tested (no NTAG tag available) |

## Quick start

```bash
pio run -t upload       # firmware (defaults to env:nodemcuv2)
pio run -t uploadfs     # web UI (data/ -> LittleFS)
pio device monitor      # 115200
```

Explicit envs: `pio run -e nodemcuv2 ...` or `pio run -e esp32c3 ...`.
On this machine PlatformIO lives at `~/.platformio/penv/bin/pio`.

1. Wire the PN532 per [docs/wiring.md](docs/wiring.md): SPI, **SW1 OFF / SW2 ON**,
   3V3, SCK=D5, MISO=D6, MOSI=D7, SS=D1. (I2C fallback: `PN532_USE_SPI 0`,
   SW1 ON / SW2 OFF, SDA=D2, SCL=D1.)
2. First boot with no stored WiFi starts the open AP **PN532-Setup**.
3. Connect to it and open <http://192.168.4.1/>.
4. Set your WiFi credentials in **Network** — the board reboots and joins your LAN
   (also reachable as `http://pn532.local/`).

## Using the UI

- **Live tag** — UID, model, capacity, NDEF records and a raw page/block grid.
- **Clone** — scan the source and press *Set as clone source*, then press
  *Clone source → target*. If no tag is on the reader the clone is **armed**:
  place the target and it runs automatically. The result panel reports matched
  pages/blocks, UID status and any mismatches.
- **Write records** — build a Text and/or URI record list and write it to the
  current Type 2 tag.
- **History** — the last 4 tags on ESP8266 (8 on ESP32); click one to reuse it
  as a clone source.
- **Log** — firmware events streamed over WebSocket.

## Clone rules

- **Data clone** works on any target of the same family with enough capacity.
  NTAG page 3 (capability container) and manufacturer pages are preserved.
- **Full clone (UID + data)** needs a UID-writable tag: Gen2/CUID/FUID Mifare
  Classic "magic" blanks, or UID-changeable NTAG clones. Genuine NTAG/Ultralight
  and Mifare Classic UIDs are factory-locked; the data is still cloned and the
  result reports `ERR_UID_LOCKED`.
- Gen1a magic cards are not supported — they need backdoor commands the PN532
  firmware does not expose.
- Mifare sector trailers are skipped unless *Include trailers* is enabled,
  because writing them changes sector keys. Blocks whose key is not the default
  `FF FF FF FF FF FF` cannot be read or written.
- Only clone tags you own or are authorised to copy.

## Wiring (SPI, default)

| PN532 | NodeMCU |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCK | D5 (GPIO14) |
| MISO | D6 (GPIO12) |
| MOSI | D7 (GPIO13) |
| SS / NSS | D1 (GPIO5) |
| RSTO | 3V3 |

DIP switches: **SW1 OFF, SW2 ON** (SPI), power-cycle after changing.
I2C fallback and all pin restrictions: [docs/wiring.md](docs/wiring.md).

## Layout

```
platformio.ini          env:nodemcuv2 (default) + env:esp32c3, LittleFS
include/config.h        pins (per MCU), timings, version
src/main.cpp            boot + service wiring
src/nfc/NfcReader.*     PN532 init, target polling, raw page/block I/O
src/nfc/TagReader.*     tag identification + full dump
src/nfc/TagWriter.*     NDEF write, clone, verification
src/nfc/Ndef.*          NDEF/Type-2 TLV encode + parse
src/app/Scanner.*       presence/debounce state machine
src/app/TagService.*    snapshot/history/source state, clone orchestration
src/app/Settings.*      WiFi credentials (EEPROM on ESP8266, NVS on ESP32)
src/app/Net.*           STA with AP fallback, mDNS
src/web/WebApp.*        HTTP + WebSocket API, static file serving
data/                   web UI (index.html, app.js, style.css)
test/host/              NDEF encoder/parser tests, run without hardware
docs/                   wiring + API reference
```

## Verified hardware

- NodeMCU 1.0 (ESP-12E, 4 MB), CH340 USB bridge
- Elechouse PN532 V3 over SPI, DIP **SW1 OFF / SW2 ON**, power-cycled
- Mifare Classic 1K card + fob: dump 1024 B, data-only clone 512 B written,
  47/47 blocks verified, 16 sector trailers skipped
- UID write correctly rejected on genuine cards (`ERR_UID_LOCKED`)

## Resource notes

- ESP8266: 80 KB RAM, 4 MB flash. The build uses ~37 KB of static RAM and
  ~418 KB of the 1 MB app partition; LittleFS gets 1 MB for the ~26 KB UI.
  History is capped at 4 snapshots to leave heap for JSON and WebSocket clients.
- The firmware supports both MCUs from one codebase; board differences are
  isolated to pin definitions, `Settings`, the web-server type and `Wire.begin`.

## Tests

NDEF encode/parse logic runs on the host:

```bash
bash test/host/run.sh
```

If linking fails on macOS with a `libSystem.tbd` architecture error, point at an
older SDK:

```bash
CXXFLAGS="-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.2.sdk -mmacosx-version-min=14.0" \
  bash test/host/run.sh
```

Hardware-in-the-loop checks (tag matrix, wrong keys, tag pulled mid-write) are
listed in [docs/wiring.md](docs/wiring.md#bring-up-checklist) plus the clone
matrix above.

## Security notes

The UI and API are unauthenticated on the local network. Use it on a trusted
network, set a WiFi password, and don't expose port 80/81 to the internet.
WiFi credentials are stored in flash; the setup AP is open by design so a new
board can be provisioned.
