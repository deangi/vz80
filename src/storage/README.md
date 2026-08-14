# storage/ — SD card + config + disk images

- split INI config loader (wificonfig.ini and z80config.ini)
- `[system] prom=` / `prom_addr=` loads an optional PROM binary into Z80 RAM
  at cold boot (then trap stubs at 0xF02B are overlaid)
- Sector-buffered `.dsk` floppy I/O (77×26×128)
- `create floppy` writes a 256256-byte image filled with `0xE5` (CP/M 2.2
  unused directory / FORMAT). Do not zero-fill: `0x00` in the directory
  user byte looks like an active user-0 file.
- Hard-disk image I/O is deferred until the CP/M BIOS/DPB geometry supports
  it (see `src/cpm/hdd8mb.md`)
