# cpm/ — CP/M 2.2 BIOS / BDOS emulation

Current disk support is the Altair/iCOM CP/M 2.2 path:
- Optional SD PROM image via `[system] prom=` / `prom_addr=` in z80config.ini
- BIOS/PROM trap stubs at 0xF02B..0xF03A (always overlaid after PROM load)
- SELDSK / SETTRK (16-bit BC) / SETSEC / SETDMA / READ / WRITE dispatch
- Floppy: 77×26×128 `.dsk` (IBM-3740 / FD3712 DPB)
- HDD: 2048×32×128 `.hdd` (8 MB); host patches guest DPH/DPB/ALV after boot
  (`installHddDpbs`, see `hdd8mb.md`). Blank images are `0xE5`-filled.

## Line printer (LST:)

Host emulates 88-LPC ports used by deramp Altair CP/M:
- `IN A,(02h)` — status, **bit1=1** when the capture FIFO can accept a byte
- `OUT (03h),A` — 7-bit data into `lp_capture` → next free `/LPn.TXT` on SD

After CP/M reaches a quiet prompt, the host overlays LIST/LISTST at `0xF040`
and patches the BIOS jump table (and IOBYTE LST:=LPT:) so `LST:` / `^P`
always use those ports. See `list_lpc.asm`. Each guest reset opens the next
`LPn.TXT` session (reuses an empty file when present).
