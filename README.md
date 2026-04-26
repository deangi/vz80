# vZ80 — Z80 / CP/M 2.2 emulator for ESP32 CYD2USB

Arduino sketch that turns a "Cheap Yellow Display" (CYD2USB variant: dual USB,
ST7789 panel) into a Z80 microcomputer running CP/M 2.2, with TFT console,
touch control panel, on-screen keyboard, telnet keyboard input over WiFi, and
VT-100 terminal emulation. Originally targeted the ESP32-2432S028R (ILI9341);
now targets the CYD2USB.

## Hardware
- ESP32-WROOM-32 dual-core, 240 MHz (no PSRAM on this variant)
- ST7789 TFT, 320x240 (HSPI: MOSI=13, MISO=12, SCK=14, CS=15, DC=2, BL=21)
- XPT2046 resistive touch (VSPI: SCK=25, MOSI=32, MISO=39, CS=33, IRQ=36)
- microSD slot (HSPI shared with display: MOSI=23, MISO=19, SCK=18, CS=5)
- On-board RGB LED on GPIO 4/16/17 (active LOW)

## Build environment
- **Arduino IDE 2.x** (2.3 or later recommended)
- ESP32 board support: install via Boards Manager, select **ESP32 Dev Module**
- Upload speed: 921600, Flash: 4MB, Partition: "Huge APP (3MB No OTA/1MB SPIFFS)"

### Required libraries (Library Manager)
| Library | Author | Use |
|---|---|---|
| LovyanGFX | lovyan03 | Display + touch driver (≥ 1.2.19) |
| ArduinoJson | Benoit Blanchon | `/config.json` parsing (≥ 7.4.3) |
| SimpleFTPServer | Renzo Mischianti | optional, only if `ENABLE_FTP=1` (≥ 3.0.2) |

### Why LovyanGFX (not TFT_eSPI)
This CYD variant puts the touch controller on a **separate SPI bus (VSPI:
SCK=25, MOSI=32, MISO=39, CS=33)** from the display (HSPI). TFT_eSPI assumes
shared-bus touch and needs library-folder config edits. LovyanGFX supports
dual-bus setups cleanly and all config lives inside this sketch's
`ESP32_SPI_9341.h` - nothing to edit in the library folder.

## Milestones
- M1 — splash + SD: boots, shows banner, mounts SD, reports status. ✅
- M2 — console: 80x24 back-buffer, 5x7 font, horizontal scroll viewport. ✅
- M3 — Z80 core: superzazu/z80 (MIT), 64 KB RAM (heap-allocated), Arduino loop + Z80 task pinned to **core 1**, WiFi/lwIP runs on core 0 (Espressif default). StreamBuffer between Z80 and display/keyboard. ✅
- M4 — CP/M 2.2 (Altair iCOM 3712): `DiskImage` + `AltairBios` (6 PROM trap stubs) + 88-SIO (inverted, port 0/1) + 88-2SIO (non-inverted, 0x10/0x11). ✅
- M5 — Touch control panel + mount modal: top 48 px holds 3 buttons: `SETUP` | `KBD` | `SCROLL`. `SETUP` opens a popup with `REBOOT` / `MOUNT` / `CLEAR`. `MOUNT` opens a full-screen overlay listing `.dsk`/`.hdd` files from SD root with drive tabs A..D and OK/UNMNT/CANCEL. The Z80 task is suspended while a modal is up. Touch calibration is persisted to `/touch_cal.bin`. ✅
- M6 — Bluetooth keyboard: **REMOVED**. The original ESP32 in the CYD2USB has insufficient internal DRAM to host both WiFi and Bluedroid; with WiFi up there's only ~16 KB largest-contiguous heap, and `esp_bluedroid_init()` needs >15 KB and crashes. No PSRAM on this board to relocate the 64 KB Z80 RAM. Keyboard input now comes from telnet (M7) or the on-screen keyboard (M8). ❌
- M7 — WiFi STA + telnet (port 23) with VT-100 Tier-1 escape parsing on the LCD console (CUP/EL/ED/NEL/DECSC/DECRC). FTP server scaffolded but disabled via `ENABLE_FTP=0` in `src/network/ftp_server.h`. ✅
- M8 — On-screen US-QWERTY keyboard: modal overlay (full 320 px wide × 144 px tall, bottom of screen). Tap `KBD` to toggle. Sticky one-shot SHIFT and CTRL. Console viewport shrinks to the last 6 buffer rows so the cursor stays visible above the keyboard. ✅

## Layout
```
vZ80/
├── vZ80.ino            # Arduino sketch entry
├── ESP32_SPI_9341.h    # LovyanGFX config for this CYD (HSPI display + VSPI touch)
├── src/
│   ├── z80/            # Z80 CPU core (M3) — superzazu wrapper + RAM
│   ├── cpm/            # Altair iCOM 3712 BIOS PROM trap stubs (M4)
│   ├── display/        # 80x24 console + VT-100 Tier-1 parser (M2/M7)
│   ├── ui/             # touch panel + setup/mount/keyboard modals (M5/M8)
│   ├── storage/        # SD + .dsk/.hdd disk image reader (M4)
│   ├── input/          # (empty — see M6 note above)
│   ├── network/        # WiFi STA + telnet, FTP scaffolded (M7)
│   └── hw/             # on-board RGB LED driver
├── sdcard/             # shipped templates: default config.json + altair48k.dsk
└── data/               # local SD-card layout (gitignored, user state)
```
Arduino IDE 2.x compiles any `.cpp`/`.h` placed under `src/` automatically.

## Design decisions
- **HDD file format**: one `.hdd` per drive, up to 8MB (CP/M 2.2 single-drive max).
- **Console**: 80 columns logical, 53 visible on the 320-wide display, horizontal
  scroll via touch buttons to reveal columns 0-79.
- **Z80 core**: integrate `z80.h` from superzazu (MIT) rather than writing from
  scratch — already passes ZEXALL.

## Known boot disks (for M4 testing)
- `sdcard/altair48k.dsk` (shipped in this repo) — 256,256 bytes, deramp.com
  `CPM22v1.0-3712-48K.DSK`. Standard iCOM 3712 / IBM 3740 8" SS-SD:
  **77 tracks × 26 sectors × 128 bytes**. Copy onto your SD card; the
  shipped `sdcard/config.json` mounts it as A:.
- `idrive.hdd` (not shipped) — 8,388,608 bytes. Drop on the SD card as
  C: once CP/M boots.
