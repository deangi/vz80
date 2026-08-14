#pragma once
#include <Arduino.h>
#include <SD_MMC.h>
#include <FS.h>

// Raw disk image on SD: flat (track * sectorsPerTrack + (sector-1)) * sectorBytes.
// Floppy default: iCOM 3712 / IBM 3740 8" SS-SD — 77×26×128 = 256,256 bytes.
// HDD: 2048×32×128 = 8,388,608 bytes (CP/M 2.2 max). See config.h CPM_* .
//
// Sectors are 1-based (CP/M convention). Track is 0-based (16-bit for HDD).
class DiskImage {
public:
    bool open(const char* path,
              uint16_t tracks         = 77,
              uint8_t  sectorsPerTrack = 26,
              uint16_t sectorBytes    = 128,
              bool     writable       = true);
    void close();
    bool isOpen() const { return file_; }

    bool readSector (uint16_t track, uint8_t sector, uint8_t* buf);
    bool writeSector(uint16_t track, uint8_t sector, const uint8_t* buf);

    uint16_t tracks()         const { return tracks_; }
    uint8_t  sectorsPerTrack()const { return sectorsPerTrack_; }
    uint16_t sectorBytes()    const { return sectorBytes_; }
    const char* path()        const { return path_; }

private:
    fs::File file_;
    char     path_[64]{};
    uint16_t tracks_ = 0;
    uint8_t  sectorsPerTrack_ = 0;
    uint16_t sectorBytes_ = 128;
    bool     writable_ = false;

    uint32_t offsetOf(uint16_t track, uint8_t sector) const {
        return ((uint32_t)track * sectorsPerTrack_ + (sector - 1)) * sectorBytes_;
    }
};
