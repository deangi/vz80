# vZ80

vZ80 is a Z80 / CP/M emulator sketch for the Freenove ESP32-S3 2.8" Display.
It shares the menu, configuration, Telnet, FTP, status line, and SD-card
layout conventions used by the companion vApple2, vpdp1140, and v8088 sketches.

## Configuration

Runtime configuration is split across two INI files on the SD card:

- `/wificonfig.ini` contains WiFi, Telnet, and FTP settings.
- `/z80config.ini` contains the emulator title, console mode, boot text, disk
  image paths, and boot drive.

Named variants use the same pattern:

- `wificonfig-NAME.ini`
- `z80config-NAME.ini`

The Settings menu can copy a selected variant over the active config file.

## Console

The TFT console supports VT100-style escape sequences by default and can also
interpret ADM-3A sequences. Select the mode in `/z80config.ini`:

```ini
[console]
terminal = vt100
; terminal = adm3a
boot_text = ""
```

The on-screen keyboard is retained for direct CP/M input.

## File Transfer

File transfer is provided by FTP. The old browser-based file-transfer path from
the earlier CYD sketch is not used in this version.

## SD Card

Example SD-card config files live in `Z80SdCard/`. Disk images are intentionally
ignored by Git; copy the required CP/M disk images to the SD card separately.
