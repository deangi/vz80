# storage/ — SD card + config + disk images

M1 boots SD here. Later milestones add:
- split INI config loader (wificonfig.ini and z80config.ini)
- Sector-buffered `.dsk` floppy image I/O
- Hard-disk image I/O is deferred until the CP/M BIOS/DPB geometry supports it
