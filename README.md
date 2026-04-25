# vZ80 — Z80 / CP/M 2.2 emulator for ESP32 CYD

Arduino sketch that turns a "Cheap Yellow Display" (ESP32-2432S028R) into a
Z80 microcomputer running CP/M 2.2, with TFT console, touch control panel,
Bluetooth keyboard, and WiFi/FTP for disk-image transfer.

## Hardware
- ESP32-WROOM-32 dual-core, 240 MHz
- ILI9341 TFT, 320x240 (HSPI)
- XPT2046 resistive touch (shared HSPI, CS=33)
- microSD slot (VSPI: MOSI=23, MISO=19, SCK=18, CS=5)

## Build environment
- **Arduino IDE 2.x** (2.3 or later recommended)
- ESP32 board support: install via Boards Manager, select **ESP32 Dev Module**
- Upload speed: 921600, Flash: 4MB, Partition: "Huge APP (3MB No OTA/1MB SPIFFS)"

### Required libraries (Library Manager)
| Library | Author | Use |
|---|---|---|
| LovyanGFX | lovyan03 | Display + touch driver |
| ArduinoJson | Benoit Blanchon | config.json parsing (later milestone) |

Later milestones will add ESP32-FTP-Server (schreibfaul1) and a BT HID lib.

### Why LovyanGFX (not TFT_eSPI)
This CYD variant puts the touch controller on a **separate SPI bus (VSPI:
SCK=25, MOSI=32, MISO=39, CS=33)** from the display (HSPI). TFT_eSPI assumes
shared-bus touch and needs library-folder config edits. LovyanGFX supports
dual-bus setups cleanly and all config lives inside this sketch's
`ESP32_SPI_9341.h` - nothing to edit in the library folder.

## Milestones
- M1 — splash + SD: boots, shows "vZ80 v1.0", mounts SD, reports status. ✅
- M2 — console: 80x24 back-buffer, 5x7 font, horizontal scroll viewport. ✅
- M3 — Z80 core: superzazu/z80 (MIT), 64KB RAM, dual-core FreeRTOS (emu=core 0, display=core 1), StreamBuffer between them. ✅
- M4 — CP/M 2.2 (Altair iCOM 3712): `DiskImage` + `AltairBios` (6 PROM trap stubs) + 88-SIO (inverted, port 0/1) + 88-2SIO (non-inverted, 0x10/0x11). ✅
- **M5 — Touch control panel + mount modal** *(current)*: Top 48 px is 6 touch buttons: `RUN/STOP` | `BOOT` | `CLR` | `BT` | `MNT` | `SCRL`. `MNT` opens a full-screen overlay listing `.dsk`/`.hdd` files from SD root with drive tabs A..D (A/B floppies supported, C/D greyed out) and OK/UNMNT/CANCEL. The Z80 task is suspended while the modal is open. Touch calibration is persisted to `/touch_cal.bin`; miscalibrated units can call `TouchPanel::calibrateAndSave()` once.
- M6 — Bluetooth keyboard (Classic HID preferred, BLE fallback).
- M7 — WiFi + FTP server rooted at SD.

## Layout
```
vZ80/
├── vZ80.ino            # Arduino sketch entry
├── ESP32_SPI_9341.h    # LovyanGFX config for this CYD (HSPI display + VSPI touch)
├── src/                # module stubs for later milestones
│   ├── z80/            # Z80 CPU core (M3)
│   ├── cpm/            # BIOS / BDOS  (M4)
│   ├── display/        # console renderer (M2)
│   ├── ui/             # touch controls (M5)
│   ├── storage/        # SD + disk images (M4)
│   ├── input/          # BT keyboard  (M6)
│   └── network/        # WiFi / FTP   (M7)
└── data/               # font bitmaps, default config.json
```
Arduino IDE 2.x compiles any `.cpp`/`.h` placed under `src/` automatically.

## Design decisions
- **HDD file format**: one `.hdd` per drive, up to 8MB (CP/M 2.2 single-drive max).
- **Console**: 80 columns logical, 53 visible on the 320-wide display, horizontal
  scroll via touch buttons to reveal columns 0-79.
- **Z80 core**: integrate `z80.h` (Andre Weissflog, MIT) rather than writing from
  scratch — already passes ZEXALL.

## Known boot disks (for M4 testing)
- `E:/altair48k.dsk` — 256,256 bytes, deramp.com `CPM22v1.0-3712-48K.DSK`.
  Standard iCOM 3712 / IBM 3740 8" SS-SD: **77 tracks × 26 sectors × 128 bytes**.
  Boots with DiskImage defaults; `config.json` A: points to this file.
- `E:/idrive.hdd` — 8,388,608 bytes. Drop in as C: later once CP/M boots.
