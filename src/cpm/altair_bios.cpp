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
    // Overlay after optional SD PROM load. 6 entries, 3 bytes each:
    // D3 <port> C9  (OUT (port), A ; RET)
    for (uint8_t i = 0; i < 6; ++i) {
        uint16_t addr = PROM_BASE + i * 3;
        ram[addr + 0] = 0xD3;                     // OUT (n), A
        ram[addr + 1] = TRAP_PORT_BASE + i;       // 0xC0..0xC5
        ram[addr + 2] = 0xC9;                     // RET
    }
}

bool AltairBios::installListStubs(uint8_t* ram) {
    if (!ram) return false;
    // Page 0: C3 <wboot_lo> <wboot_hi>
    if (ram[0] != 0xC3) return false;
    const uint16_t wboot = (uint16_t)ram[1] | ((uint16_t)ram[2] << 8);
    if (wboot < 3) return false;
    const uint16_t bios = (uint16_t)(wboot - 3);

    // LISTST at LIST_STUB_BASE:
    //   IN A,(02) ; AND 02h ; RET Z ; LD A,FFh ; RET
    const uint16_t listst = LIST_STUB_BASE;
    ram[listst + 0] = 0xDB;  // IN A,(n)
    ram[listst + 1] = 0x02;
    ram[listst + 2] = 0xE6;  // AND n
    ram[listst + 3] = 0x02;
    ram[listst + 4] = 0xC8;  // RET Z
    ram[listst + 5] = 0x3E;  // LD A,n
    ram[listst + 6] = 0xFF;
    ram[listst + 7] = 0xC9;  // RET

    // LIST at LIST_STUB_BASE+8 (matches deramp LPT LIST):
    //   CALL listst ; JP Z,list ; LD A,C ; AND 7Fh ; OUT (03),A ; RET
    const uint16_t list = LIST_STUB_BASE + 8;
    ram[list + 0]  = 0xCD;                         // CALL nn
    ram[list + 1]  = (uint8_t)(listst & 0xFF);
    ram[list + 2]  = (uint8_t)(listst >> 8);
    ram[list + 3]  = 0xCA;                         // JP Z,nn
    ram[list + 4]  = (uint8_t)(list & 0xFF);
    ram[list + 5]  = (uint8_t)(list >> 8);
    ram[list + 6]  = 0x79;                         // LD A,C
    ram[list + 7]  = 0xE6;                         // AND 7Fh
    ram[list + 8]  = 0x7F;
    ram[list + 9]  = 0xD3;                         // OUT (03),A
    ram[list + 10] = 0x03;
    ram[list + 11] = 0xC9;                         // RET

    // Patch BIOS jump table: +15 LIST, +45 LISTST → JMP <stub>
    const uint16_t jmp_list   = (uint16_t)(bios + 15);
    const uint16_t jmp_listst = (uint16_t)(bios + 45);
    ram[jmp_list + 0] = 0xC3;
    ram[jmp_list + 1] = (uint8_t)(list & 0xFF);
    ram[jmp_list + 2] = (uint8_t)(list >> 8);
    ram[jmp_listst + 0] = 0xC3;
    ram[jmp_listst + 1] = (uint8_t)(listst & 0xFF);
    ram[jmp_listst + 2] = (uint8_t)(listst >> 8);

    // Also force IOBYTE LST:=LPT: (bits 7:6 = 10) so stock BIOS paths agree.
    ram[0x0003] = (uint8_t)((ram[0x0003] & 0x3F) | 0x80);

    return true;
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

        case 0xC4: {  // pREAD : A=0 ok / A=1 fail (also Z flag for legacy)
            bool ok = doRead(z);
            z->a  = ok ? 0 : 1;
            z->zf = ok ? 1 : 0;
            return true;
        }

        case 0xC5: {  // pWRITE : A=0 ok / A=1 fail (also Z flag for legacy)
            bool ok = doWrite(z);
            z->a  = ok ? 0 : 1;
            z->zf = ok ? 1 : 0;
            return true;
        }

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
