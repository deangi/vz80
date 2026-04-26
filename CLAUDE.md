# vZ80 — Z80/CP/M 2.2 emulator on ESP32 CYD2USB

Repo: https://github.com/deangi/vz80 (private)

## Target hardware
**CYD2USB** (the dual-USB Cheap Yellow Display variant — USB-C + Micro-USB,
ST7789 panel). Build for "ESP32 Dev Module", partition "Huge APP". The
original ESP32-2432S028R is no longer in use; rotation, panel class, and
color flags are tuned for the new board (ST7789 + rotation=3 + BGR).

## Required Arduino libraries
- LovyanGFX (lovyan03) ≥ 1.2.19
- ArduinoJson (bblanchon) ≥ 7.4.3
- SimpleFTPServer (Renzo Mischianti) — optional, only when `ENABLE_FTP=1`

## Milestones
- ✅ M1 splash + SD
- ✅ M2 80×24 console (5×7 font, horizontal scroll viewport)
- ✅ M3 Z80 core (superzazu, MIT) + 64KB RAM (heap-allocated to keep `.bss` small)
- ✅ M4 CP/M 2.2 boot via Altair iCOM 3712 disk image
- ✅ M5 touch control panel + mount modal (rotation=3, ST7789, invert=false, rgb_order=true)
- ❌ M6 BLE HID keyboard — **REMOVED**, insufficient internal RAM (see "BT removed" gotcha below). Telnet is the keyboard input path.
- ✅ M7 WiFi STA + telnet (port 23) + VT-100 Tier-1 escape parser on the LCD console. FTP scaffolded but disabled via `ENABLE_FTP=0`.
- ✅ M8 on-screen US-QWERTY keyboard — modal overlay (bottom 144 px). Tap KBD on the top strip to toggle. Sticky one-shot SHIFT and CTRL (tap modifier → next key applies it, then auto-releases). Console viewport shrinks to the last 6 rows while the keyboard is up so the cursor stays visible. Built on `LGFX_Button`-style per-key drawing in `src/ui/keyboard_modal.{h,cpp}`.

## Build/runtime gotchas (read before changing related code)

**SPI host layout** — display=HSPI shared with SD via `bus_shared=true`,
touch=VSPI exclusively. Putting SD on `SPIClass(VSPI)` instead of HSPI
remaps VSPI's SCK/MOSI/MISO and silently kills touch — the demo1
sketch uses the same layout we do, so cross-check there if anything
weird happens with touch/display.

**DRAM budget** — Z80's 64 KB RAM is heap-allocated (`heap_caps_malloc`
in `Z80CPU::begin`) so it doesn't sit in `.bss`. Without that, BT +
WiFi + FTP overflowed `dram0_0_seg` by ~11 KB. If you grow .bss further,
the Z80 RAM is the obvious thing to push to PSRAM (board-dependent).

**BT removed (BT and WiFi cannot coexist on this board)** — Attempted M6
BLE HID keyboard support, hit a hard wall: the original ESP32 in the
CYD2USB has too little internal DRAM. With WiFi associated we have
~17 KB free heap, largest contiguous block ~16 KB. `esp_bluedroid_init()`
needs >15 KB contiguous for one of its internal allocations, returns
NULL, and crashes (LoadProhibited) on the next deref. Tearing WiFi down
at runtime frees ~34 KB of free heap *but the largest contiguous block
doesn't budge* — the heap stays fragmented, so Bluedroid still can't
init. PSRAM would solve it (move the 64 KB Z80 RAM out of internal DRAM)
but a runtime probe at boot shows this board has no PSRAM chip
(`quad_psram: PSRAM ID read error`). Net: no BT possible here. All BT
code, the `btInUse()` weak-override, the Classic memory release, the
PSRAM probe, and the BT button are gone. Keyboard input is now telnet
over WiFi only. M8 will add an on-screen keyboard for keyboard-less use.

**SimpleFTPServer storage type** — its default is `STORAGE_FFAT`, not
`STORAGE_SD`. Per-project `#define`s in `src/network/ftp_server.cpp`
do NOT propagate to the library's separately-compiled .cpp under
Arduino IDE. Edit `FtpServerKey.h` (around line 63) in the library
directory:
`#define DEFAULT_STORAGE_TYPE_ESP32  STORAGE_SD`. This change is
external to the repo and reverts on library upgrade.

**FTP enable/disable** — `src/network/ftp_server.h:11` `#define ENABLE_FTP 0`.
Set to 1 to compile FTP back in; the API stays linkable either way (no-op stubs).

## /config.json schema (lives on the SD card, not in the repo)
JSON keys use full English words, snake_case for compounds:
```json
{
  "wifi":   { "ssid": "...", "password": "..." },
  "ftp":    { "user": "esp32", "password": "esp32", "enabled": true },
  "telnet": { "enabled": true, "port": 23 },
  "drives": { "A": "altair48k.dsk", "B": "floppy1.dsk", "C": "...", "D": "" },
  "bluetooth": { "keyboard_name": "", "keyboard_mac": "" },
  "display":   { "brightness": 100, "scroll_anchor": "left" },
  "app":       { "name": "vZ80", "version": "1.0.0" }
}
```
NOT `wifi.pass` / `ftp.pass`. We've burned hours debugging the empty-
string-from-bad-key trap. When extending, mirror this style and log
post-load values for anything that affects connectivity.

## Core/task pinning
- loop + Z80 + FTP (when `ENABLE_FTP=1`) → core 1
- WiFi/lwIP → core 0
- Touch SPI on VSPI, display+SD on HSPI

## Key files
- `vZ80.ino` — setup/loop, Z80 task, button dispatch, network bring-up
- `ESP32_SPI_9341.h` — LovyanGFX panel/touch config (Panel_ST7789)
- `src/display/console.{h,cpp}` — 80×24 back-buffer + 5×7 font + VT-100 Tier-1 escape parser (CUP, EL/ED, NEL, DECSC/DECRC)
- `src/z80/z80_cpu.{h,cpp}` — wraps superzazu, heap-alloc'd RAM
- `src/cpm/altair_bios.{h,cpp}` — iCOM 3712 PROM trap stubs (port 0xC0..0xC5)
- `src/storage/disk_image.{h,cpp}` — .DSK / .HDD reader
- `src/ui/touch_panel.{h,cpp}` — 6-button strip + 8px status footer
- `src/ui/mount_modal.{h,cpp}` — disk-mount overlay
- `src/ui/setup_modal.{h,cpp}` — REBOOT / MOUNT / CLEAR popup
- `src/ui/keyboard_modal.{h,cpp}` — M8 on-screen QWERTY keyboard
- `src/network/wifi_sta.{h,cpp}` — STA connect (synchronous, blocks setup)
- `src/network/telnet_server.{h,cpp}` — TCP/23, IAC negotiation, txStream fan-out
- `src/network/ftp_server.{h,cpp}` — SimpleFTPServer wrapper, behind ENABLE_FTP
