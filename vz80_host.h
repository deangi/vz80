#pragma once
#include <stddef.h>
#include <stdint.h>
#include "src/cpm/altair_bios.h"
#include "src/storage/disk_image.h"

extern DiskImage disks[AltairBios::MAX_DRIVES];

bool vz80_mount_drive(uint8_t drive, const char* path, bool writable = true);
bool z80_ftp_path_protected(const char* path);
void vz80_request_guest_restart();
bool vz80_consume_guest_restart();
