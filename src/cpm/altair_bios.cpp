#include "altair_bios.h"

bool AltairBios::mount(uint8_t drive, DiskImage* image) {
    if (drive >= MAX_DRIVES) return false;
    drives_[drive] = image;
    return true;
}

void AltairBios::unmount(uint8_t drive) {
    if (drive < MAX_DRIVES) drives_[drive] = nullptr;
}

void AltairBios::resetState() {
    curDrive_  = 0;
    curTrack_  = 0;
    curSector_ = 1;
    dmaAddr_   = 0x0080;
}

void AltairBios::installStubs(uint8_t* ram) {
    // 6 entries, each 3 bytes: D3 <port> C9  (OUT (port), A ; RET)
    for (uint8_t i = 0; i < 6; ++i) {
        uint16_t addr = PROM_BASE + i * 3;
        ram[addr + 0] = 0xD3;                     // OUT (n), A
        ram[addr + 1] = TRAP_PORT_BASE + i;       // 0xC0..0xC5
        ram[addr + 2] = 0xC9;                     // RET
    }
}

bool AltairBios::handleOut(z80* z, uint8_t port) {
    switch (port) {
        case 0xC0:  // pSELDSK : C = drive
            curDrive_ = z->c;
            return true;

        case 0xC1:  // pSETTRK : C = track
            curTrack_ = z->c;
            return true;

        case 0xC2:  // pSETSEC : C = sector
            curSector_ = z->c;
            return true;

        case 0xC3:  // pSETDMA : BC = address (B=high, C=low)
            dmaAddr_ = ((uint16_t)z->b << 8) | z->c;
            return true;

        case 0xC4:  // pREAD : Z=success
            z->zf = doRead(z);
            return true;

        case 0xC5:  // pWRITE : Z=success
            z->zf = doWrite(z);
            return true;

        default:
            return false;
    }
}

bool AltairBios::doRead(z80* z) {
    if (curDrive_ >= MAX_DRIVES) return false;
    DiskImage* d = drives_[curDrive_];
    if (!d || !d->isOpen()) return false;
    uint8_t sec[128];
    if (!d->readSector(curTrack_, curSector_, sec)) return false;
    // Copy into Z80 RAM via the CPU's write_byte callback so we honour any
    // future memory mapping. For now ram[] is flat so a memcpy would also work.
    for (uint16_t i = 0; i < 128; ++i) {
        z->write_byte(z->userdata, dmaAddr_ + i, sec[i]);
    }
    return true;
}

bool AltairBios::doWrite(z80* z) {
    if (curDrive_ >= MAX_DRIVES) return false;
    DiskImage* d = drives_[curDrive_];
    if (!d || !d->isOpen()) return false;
    uint8_t sec[128];
    for (uint16_t i = 0; i < 128; ++i) {
        sec[i] = z->read_byte(z->userdata, dmaAddr_ + i);
    }
    return d->writeSector(curTrack_, curSector_, sec);
}
