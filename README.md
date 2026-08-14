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
`mount`, `create`, `reset`, …). `exit` returns to the CP/M console.

```text
create floppy /scratch.dsk
create hdd /cpm8mb.hdd
mount C /cpm8mb.hdd
reset
```

- `create floppy <path>` — 77×26×128 (256256 bytes, IBM 3740 / iCOM FD3712)
- `create hdd <path>` — 2048×32×128 (8388608 bytes, CP/M 2.2 max ~8 MB)
- `mount <A|B|C|D> <path> [ro]` — attach an existing `.dsk` or `.hdd`
- `dismount <A|B|C|D>` — detach
- `reset` — reboot the Z80 (needed after changing media so CP/M sees the new DPB)

Images are filled with `0xE5` (empty CP/M directory). Geometry is taken from
the file size / `.hdd` extension; you do not set tracks by hand.

## Hard disk (C:)

Keep booting from a floppy (A:) that has CP/M system tracks. A blank `.hdd`
has no CCP/BDOS; use it as a data drive.

**Telnet (one-time create, then mount):**

```text
ESC >
create hdd /cpm8mb.hdd
mount C /cpm8mb.hdd
reset
exit
```

**Persistent config** in `/z80config.ini` (example: `Z80SdCard/z80config-hdd.ini`).
Copy that variant over the active config from the Settings menu, or edit:

```ini
[disks]
a    = /altair48k-boot.dsk
b    = /floppy1.dsk
c    = /cpm8mb.hdd
d    =
boot = a
```

The on-screen disk picker also lists `.hdd` files.

After the CP/M prompt, the host patches drive C’s DPB. Serial log:

```text
C: <- /cpm8mb.hdd [hdd] (2048 trk x 32 sec x 128 byte)
CP/M HDD DPB patched for 1 drive(s) mask=0x04
```

In CP/M:

```text
STAT C:
DIR C:
PIP C:=A:*.*
```

A blank 8 MB image reports about **8168k** remaining (`STAT C:`). `DIR C:` is
empty until you copy files. Internals: `src/cpm/hdd8mb.md`.

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

This version supports CP/M floppy (`.dsk`) and 8 MB hard-disk (`.hdd`)
images (see **Hard disk (C:)** above). LIST/LST: uses 88-LPC ports
`02h`/`03h` and writes `/LPn.TXT` on the SD card.
