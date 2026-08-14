# cpm/ — CP/M 2.2 BIOS / BDOS emulation

Current disk support is the Altair/iCOM CP/M 2.2 floppy path:
- Optional SD PROM image via `[system] prom=` / `prom_addr=` in z80config.ini
- BIOS/PROM trap stubs at 0xF02B..0xF03A (always overlaid after PROM load)
- SELDSK / SETTRK / SETSEC / SETDMA / READ / WRITE dispatch
- 77 tracks x 26 sectors x 128 bytes per mounted `.dsk` image
- Guest DPB matches IBM-3740 / FD3712: 1 KiB blocks, 64 directory entries
  (DRM=63), 2 reserved tracks (OFF=2). Blank images are `0xE5`-filled.

Hard-disk image support is intentionally deferred. The ESP32 storage layer can
address larger files, but CP/M also needs matching BIOS/DPB geometry and
16-bit SETTRK before the guest can use an 8 MB C:/D: drive. Recipe:
`hdd8mb.md` (cpuville + [ciernioo](https://ciernioo.wordpress.com/2016/05/11/cpm-2-2-up-and-running/)
CBIOS walkthroughs).

## Line printer (LST:)

Host emulates 88-LPC ports used by deramp Altair CP/M:
- `IN A,(02h)` — status, **bit1=1** when the capture FIFO can accept a byte
- `OUT (03h),A` — 7-bit data into `lp_capture` → next free `/LPn.TXT` on SD

After CP/M reaches a quiet prompt, the host overlays LIST/LISTST at `0xF040`
and patches the BIOS jump table (and IOBYTE LST:=LPT:) so `LST:` / `^P`
always use those ports. See `list_lpc.asm`. Each guest reset opens the next
`LPn.TXT` session (reuses an empty file when present).
