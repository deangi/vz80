# cpm/ — CP/M 2.2 BIOS / BDOS emulation

Current disk support is the Altair/iCOM CP/M 2.2 floppy path:
- BIOS/PROM trap stubs at 0xF02B..0xF03A
- SELDSK / SETTRK / SETSEC / SETDMA / READ / WRITE dispatch
- 77 tracks x 26 sectors x 128 bytes per mounted `.dsk` image

Hard-disk image support is intentionally deferred. The ESP32 storage layer can
address larger files, but CP/M also needs matching BIOS/DPB geometry before the
guest can use larger C:/D: drives safely.
