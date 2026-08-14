# storage/ — SD card + config + disk images

- split INI config loader (wificonfig.ini and z80config.ini)
- `[system] prom=` / `prom_addr=` loads an optional PROM binary into Z80 RAM
  at cold boot (then trap stubs at 0xF02B are overlaid)
- Sector-buffered `.dsk` floppy I/O (77×26×128) and `.hdd` (2048×32×128)
- `create floppy` / `create hdd` write `0xE5`-filled images
- Hard-disk guest DPB is patched at runtime (`installHddDpbs`); see
  `src/cpm/hdd8mb.md`
