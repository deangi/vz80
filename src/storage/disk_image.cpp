#include "disk_image.h"
#include <string.h>

bool DiskImage::open(const char* path, uint16_t tracks, uint8_t spt,
                     uint16_t bytes, bool writable) {
    close();
    strncpy(path_, path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = '\0';
    tracks_          = tracks;
    sectorsPerTrack_ = spt;
    sectorBytes_     = bytes;
    writable_        = writable;

    // "r+" = read+write without truncation. FILE_WRITE ("w") would erase the
    // disk image on open, so we never use it for an existing .dsk.
    file_ = SD_MMC.open(path_, writable ? "r+" : "r");
    if (!file_) return false;

    uint32_t expected = (uint32_t)tracks_ * sectorsPerTrack_ * sectorBytes_;
    if (file_.size() < expected) {
        Serial.printf("[DSK] %s too small: %u < %u\n",
                      path_, (unsigned)file_.size(), (unsigned)expected);
        close();
        return false;
    }
    return true;
}

void DiskImage::close() {
    if (file_) file_.close();
    tracks_ = 0;
    sectorsPerTrack_ = 0;
}

bool DiskImage::readSector(uint16_t track, uint8_t sector, uint8_t* buf) {
    if (!file_) return false;
    if (track >= tracks_ || sector < 1 || sector > sectorsPerTrack_) return false;
    uint32_t off = offsetOf(track, sector);
    if (!file_.seek(off)) return false;
    return file_.read(buf, sectorBytes_) == (int)sectorBytes_;
}

bool DiskImage::writeSector(uint16_t track, uint8_t sector, const uint8_t* buf) {
    if (!file_ || !writable_) return false;
    if (track >= tracks_ || sector < 1 || sector > sectorsPerTrack_) return false;
    uint32_t off = offsetOf(track, sector);
    if (!file_.seek(off)) return false;
    size_t n = file_.write(buf, sectorBytes_);
    file_.flush();
    return n == sectorBytes_;
}
