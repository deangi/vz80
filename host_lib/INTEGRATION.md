# host_lib (vZ80 Freenove)

Snapshot of vpdp1170 host components used by vZ80. **Freenove ESP32-S3 2.8" only** — no Elecrow CrowPanel, LovyanGFX, GT911, or SDSPI.

## Arduino compile rule

Sketch-root shims pull in `host_lib/` sources (Arduino compiles sketch-root `.cpp` only):

- `fifo.h` → `host_lib/util/fifo.h`
- `gfx.h` → `host_lib/gfx/gfx.h` (TFT_eSPI only)
- `sd_fs.h` → `#define SD_FS SD_MMC`
- `console.cpp` → `host_lib/console/console.cpp`
- `host_time.cpp` → `host_lib/time/host_time.cpp`
- `host_lib_build.cpp` → storage guard, log, shell, TelnetPipe, WiFi/net_task/net_ini, ADM-3A
- `host_boot_input_build.cpp` → `boot_input.cpp` (separate TU)

Not included: `board/*`, `touch/*`, `sd/sd_fs.*`, `boot_script`.

Included capture: `capture/lp_capture.*` via sketch-root `lp_capture.cpp`
(88-LPC `IN 02`/`OUT 03` → FIFO → `/LPn.TXT`).

## Console

INI `[console] terminal=` selects `HOST_TERM_ADM3A` or `HOST_TERM_VT100`. **Default ADM-3A** (WordStar). Do not force VT100.

## Telnet shell

`ESC` `>` enters the host shell (FS + MediaOps A–D + guest reset). No PDP pack.
`create` accepts only `floppy` for now (`create floppy <path>` → 77×26×128
`.dsk` filled with `0xE5` for a blank CP/M 2.2 directory).

Optional `[system] prom=` / `prom_addr=` in `z80config.ini` loads a PROM
binary into Z80 RAM before `installStubs()`.
