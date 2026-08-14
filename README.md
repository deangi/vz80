# vZ80

vZ80 is a Z80 / CP/M emulator sketch for the **Freenove ESP32-S3 2.8" Display**
only (TFT_eSPI / FT6336U / SD_MMC). Elecrow CrowPanel is not supported.
It shares the menu, configuration, Telnet, FTP, status line, and SD-card
layout conventions used by the companion vApple2, vpdp1170, and v8088 sketches.

## Configuration

Runtime configuration is split across two INI files on the SD card:

- `/wificonfig.ini` contains WiFi, NTP, Telnet, and FTP settings.
- `/z80config.ini` contains the emulator title, optional PROM BIOS path,
  console mode, boot text, disk image paths, and boot drive.

Named variants use the same pattern:

- `wificonfig-NAME.ini`
- `z80config-NAME.ini`

The Settings menu can copy a selected variant over the active config file.

## PROM BIOS

The guest CP/M BIOS lives on the boot `.dsk` system tracks. The host also
emulates the iCOM 3712 PROM jump table at `0xF02B` (SELDSK..WRITE traps).

To try a different PROM image, put a binary on the SD card and name it in
`/z80config.ini`:

```ini
[system]
title     = vZ80 CP/M Apps
prom      = /icom-prom.bin
prom_addr = 0xF000
```

Leave `prom` blank to use the built-in trap stubs only. After the file is
copied into Z80 RAM, the host still overlays the six trap stubs at `0xF02B`
so SD `.dsk` I/O keeps working. If `prom=` is set but the file is missing,
boot continues with the internal PROM copy and logs that on the serial
console.

## Console

The TFT console defaults to ADM-3A (WordStar / CP/M apps) and can also
interpret VT100 sequences. Select the mode in `/z80config.ini`:

```ini
[console]
terminal = adm3a
; terminal = vt100
boot_text = ""
```

`boot_text` is injected after CP/M output goes quiet at a prompt.

The on-screen keyboard is retained for direct CP/M input.

## Telnet shell

From a Telnet session, type `ESC` then `>` to open the host shell (`ls`,
`mount A /image.dsk`, `reset`, …). `exit` returns to the CP/M console.

`create` builds a blank CP/M 2.2 floppy image (77×26×128, IBM 3740 / iCOM
FD3712). The only `<type>` for now is `floppy`. The image is filled with
`0xE5` (FORMAT / unused directory), not zeros:

```text
create floppy /scratch.dsk
```

That initializes reserved tracks 0–1 and a 64-entry empty directory so CP/M
can `DIR` / save files immediately after `mount`.

## Line printer (LST:)

CP/M `LST:` / `^P` goes to the emulated 88-LPC (`IN 02h` status, `OUT 03h`
data). Bytes are queued and flushed to `/LP0.TXT`, `/LP1.TXT`, … on the SD
card (new session on each Z80 reset). After boot, the host patches the guest
BIOS LIST/LISTST vectors to those ports (see `src/cpm/list_lpc.asm`).

## File Transfer

File transfer is provided by FTP. The old browser-based file-transfer path from
the earlier CYD sketch is not used in this version.

## SD Card

Example SD-card config files live in `Z80SdCard/`. Disk images are intentionally
ignored by Git; copy the required CP/M disk images to the SD card separately.

This version supports standard CP/M floppy disk images only. An 8 MB HDD
needs a forked guest BIOS/DPB plus 16-bit SETTRK on the host — see
`src/cpm/hdd8mb.md`. LIST/LST: uses 88-LPC ports `02h`/`03h` and writes
`/LPn.TXT` on the SD card (same capture path as vpdp LP0).
