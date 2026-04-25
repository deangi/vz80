# cpm/ — CP/M 2.2 BIOS / BDOS emulation

Placeholder. M4 will implement:
- BIOS jump table at 0x0000 / warm-boot vectors
- BDOS entry trap at 0x0005
- Disk geometry tables (A:/B: floppy, C:/D: 8MB HDD)
- Sector read/write dispatch to `../storage/`
