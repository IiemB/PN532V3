# Wiring: PN532 V3 (Elechouse) ↔ NodeMCU ESP8266

Default target is **NodeMCU 1.0 / ESP-12E**. The firmware currently uses
**SPI** (selected in [include/config.h](../include/config.h) with
`PN532_USE_SPI 1`); I2C is kept as a fallback.

## DIP switches (on the PN532 V3 board)

| Interface | SW1 (HIS0) | SW2 (HIS1) |
|---|---|---|
| HSU (UART) | OFF | OFF |
| I2C | ON | OFF |
| **SPI (current)** | **OFF** | **ON** |

The interface is latched at power-up: change the switches with power removed,
then power the module again. **This was the bring-up blocker** — with the DIP
still effectively in I2C mode the PN532 answers neither bus. Verified working:
SPI, SW1 OFF / SW2 ON, RSTO tied to 3V3. The Elechouse manual (V2, same HIS0/HIS1 table)
is the source for this mapping.

## SPI on NodeMCU (default)

| PN532 V3 pin | NodeMCU pin | GPIO | Notes |
|---|---|---|---|
| VCC | **3V3** | — | SPI side is 3.3 V TTL; never 5 V here |
| GND | GND | — | Common ground |
| SCK | **D5** | GPIO14 | Hardware SPI clock |
| MISO | **D6** | GPIO12 | Boot-strap pin: leave the module at 3.3 V, no extra pull-up |
| MOSI | **D7** | GPIO13 | |
| SS / NSS | **D1** | GPIO5 | Chip select (any free GPIO works) |
| RSTO | 3V3 | — | Tie high (or leave; the module has a pull-up). Not driven in SPI mode |
| IRQ | — | — | Not used |

```
PN532 V3                 NodeMCU
+---------+              +------------------+
|  VCC o--|--------------|--o 3V3           |
|  GND o--|--------------|--o GND           |
|  SCK o--|--------------|--o D5  (GPIO14)  |
| MISO o--|--------------|--o D6  (GPIO12)  |
| MOSI o--|--------------|--o D7  (GPIO13)  |
|  SS o--|--------------|--o D1  (GPIO5)   |
| RSTO o--|--------------|--o 3V3           |
+---------+              +------------------+
```

Notes:
- The Elechouse module prints the SPI pins on the board (`MOSI`, `MISO`, `SCK`,
  `SS`/`NSS`, `3.3V`, `GND`, `RST`). Look for those labels — some V3 boards have
  a separate 4-pin I2C header (`VCC GND SDA SCL`) and a second header for SPI.
- **D8/GPIO15 is not used for SS** — it is a boot strap and must be low at boot.
- GPIO12 (MISO) is also a boot strap (flash voltage). Keep the PN532 powered at
  3.3 V and do not add pull-ups on MISO; if the board stops booting after
  wiring SPI, disconnect MISO and check.
- Hardware SPI on ESP8266 is fixed to SCK=14, MISO=12, MOSI=13. Only SS is free.
- Keep wires short (< 15 cm) at 1 MHz.

## Power notes

- The PN532 draws short current spikes up to ~150 mA while the RF field is on.
- Add **100 µF electrolytic + 100 nF ceramic** across 3V3/GND near the module.
  The NodeMCU AMS1117 can usually supply the peak, but a long/thin USB cable
  will sag. If the board resets when a tag enters the field, power the PN532
  from a separate 3.3 V LDO.
- Keep the antenna clear of the ESP8266, USB cable and metal; 1–2 cm minimum.

## NodeMCU pin rules

| Pins | Use |
|---|---|
| D5 (GPIO14), D6 (GPIO12), D7 (GPIO13) | Hardware SPI — used here |
| D1 (GPIO5) | SS — used here |
| D2 (GPIO4) | Free (I2C SDA in fallback mode) |
| D0 (GPIO16) | No interrupt, deep-sleep pin — avoid for control signals |
| D3 (GPIO0), D4 (GPIO2), D8 (GPIO15) | Boot strapping / onboard LED — do not load |
| GPIO6–GPIO11 | Internal flash — not available |

The ESP8266 has a single UART, used by the USB serial monitor. Keep it free.

## Fallback: I2C

Set `PN532_USE_SPI 0` in [include/config.h](../include/config.h), DIP
**SW1 ON / SW2 OFF**, and wire:

| PN532 V3 | NodeMCU | GPIO |
|---|---|---|
| VCC | 3V3 | — |
| GND | GND | — |
| SDA | D2 | GPIO4 |
| SCL | D1 | GPIO5 |
| IRQ (opt) | D5 | GPIO14 |
| RSTO (opt) | D7 | GPIO13 |

The firmware caps the ESP8266 I2C clock-stretch limit at 20 ms; without that a
misbehaving PN532 can block startup for minutes.

## Alternate: ESP32-C3 SuperMini

Build with `pio run -e esp32c3`. That env defaults to I2C:

| PN532 | ESP32-C3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO4 |
| SCL | GPIO5 |
| IRQ (opt) | GPIO6 |
| RSTO (opt) | GPIO7 |

## Bring-up checklist

1. DIP set to SPI, power-cycle the module, 3.3 V and common ground.
2. `pio run -e nodemcuv2 -t upload && pio device monitor`
3. Expect:
   ```
   PN532V3 0.1.0 (NodeMCU ESP8266)
   [nfc] bus: SPI (SCK=D5/14, MISO=D6/12, MOSI=D7/13, SS=D1/5)
   [nfc] probing PN532 over SPI...
   [nfc] PN532 IC 0x32 firmware 1.6 (support 0x07)
   [nfc] ready (ISO14443A, SPI)
   ```
4. No `IC 0x32` line → check DIP power-cycle, MOSI/MISO order, SS, 3.3 V, ground.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `no response from PN532` | DIP not SPI, MISO/MOSI swapped, SS on the wrong pin, no common ground |
| Board won't boot with SPI wired | GPIO12 (MISO) pulled high at boot — remove pull-ups, check module power |
| `SAMConfiguration failed` | SPI wiring marginal — shorten wires, check 3.3 V and ground |
| Boot loop / garbage serial | 3.3 V sag; add bulk capacitor, try `upload_speed = 115200` |
| Upload fails / port missing | Hold **FLASH/BOOT** during upload; install the CH340 driver |
| Web UI missing | Run `pio run -e nodemcuv2 -t uploadfs` |
| Port disappears after opening a monitor | CH340 driver quirk on macOS — replug USB, then reflash |
