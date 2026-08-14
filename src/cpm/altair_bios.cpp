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

static uint16_t rd16(const uint8_t* ram, uint16_t addr) {
    return (uint16_t)ram[addr] | ((uint16_t)ram[addr + 1] << 8);
}

static void wr16(uint8_t* ram, uint16_t addr, uint16_t v) {
    ram[addr]     = (uint8_t)(v & 0xFF);
    ram[addr + 1] = (uint8_t)(v >> 8);
}

static bool dpbMatches(const uint8_t* ram, uint16_t addr, const uint8_t* sig) {
    for (int i = 0; i < 15; ++i) {
        if (ram[addr + i] != sig[i]) return false;
    }
    return true;
}

static constexpr uint16_t HDD_SECTRAN = 0xF100;
static constexpr uint16_t HDD_XLT     = 0xF110;
static constexpr uint16_t HDD_DPB     = 0xF130;
static constexpr uint16_t HDD_ALV0    = 0xF140;

static void logIoFail(const char* op, uint8_t drive, uint16_t track, uint8_t sector) {
    static uint8_t n = 0;
    if (n >= 12) return;
    ++n;
    Serial.printf("[cpm] %s fail %c: trk=%u sec=%u\r\n",
                  op, 'A' + drive, (unsigned)track, (unsigned)sector);
}

int AltairBios::installHddDpbs(uint8_t* ram, uint8_t drive_mask) {
    if (!ram || drive_mask == 0) return 0;

    // IBM 3740 / iCOM floppy DPB (shared by stock Altair CP/M):
    // SPT=26 BSH=3 BLM=7 EXM=0 DSM=242 DRM=63 AL0=C0 AL1=00 CKS=16 OFF=2
    static const uint8_t kFloppyDpb[15] = {
        0x1A, 0x00, 0x03, 0x07, 0x00, 0xF2, 0x00, 0x3F, 0x00,
        0xC0, 0x00, 0x10, 0x00, 0x02, 0x00
    };

    // Host 8 MB DPB (see hdd8mb.md)
    static const uint8_t kHddDpb[15] = {
        0x20, 0x00,       // SPT = 32
        0x05,             // BSH = 5 (4 KiB)
        0x1F,             // BLM = 31
        0x01,             // EXM = 1
        0xFD, 0x07,       // DSM = 2045
        0xFF, 0x01,       // DRM = 511
        0xF0, 0x00,       // AL0/AL1
        0x00, 0x00,       // CKS = 0 (fixed disk)
        0x02, 0x00        // OFF = 2
    };

    // SELDSK ends with: LD HL,dph0 / LD E,A / LD D,0 / ADD HL,DE / RET
    int dph0 = -1;
    for (int a = 0x8000; a <= 0xF000; ++a) {
        if (ram[a] != 0x21 || ram[a + 3] != 0x5F || ram[a + 4] != 0x16 ||
            ram[a + 5] != 0x00 || ram[a + 6] != 0x19 || ram[a + 7] != 0xC9) {
            continue;
        }
        const uint16_t cand = rd16(ram, (uint16_t)(a + 1));
        if (cand < 0x8000 || cand > 0xF000) continue;
        dph0 = (int)cand;
        break;
    }
    if (dph0 < 0) {
        // Four DPH slots sharing one floppy DPB pointer at +10.
        for (int a = 0x8000; a <= 0xF000; ++a) {
            const uint16_t dpb_ptr = rd16(ram, (uint16_t)(a + 10));
            if (dpb_ptr < 0x8000 || !dpbMatches(ram, dpb_ptr, kFloppyDpb)) continue;
            bool ok = true;
            for (uint8_t d = 1; d < MAX_DRIVES; ++d) {
                if (rd16(ram, (uint16_t)(a + d * 16 + 10)) != dpb_ptr) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;
            dph0 = a;
            break;
        }
    }
    if (dph0 < 0) return 0;

    // After 256-byte PROM at 0xF000..0xF0FF (LIST stubs occupy 0xF040..0xF053):
    //   0xF100 SECTRAN: identity 1..32 if DE=HDD_XLT, else original BIOS
    //   0xF110 XLT sentinel / 32-byte identity
    //   0xF130 DPB
    //   0xF140 ALV0 .. +256 per drive
    for (int i = 0; i < 32; ++i) ram[HDD_XLT + i] = (uint8_t)(i + 1);
    for (int i = 0; i < 15; ++i) ram[HDD_DPB + i] = kHddDpb[i];

    if (ram[0] == 0xC3) {
        const uint16_t wboot = rd16(ram, 1);
        if (wboot >= 3) {
            const uint16_t jmp_sectran = (uint16_t)(wboot - 3 + 48);
            uint16_t orig = 0;
            if (ram[jmp_sectran] == 0xC3) orig = rd16(ram, (uint16_t)(jmp_sectran + 1));
            // LD A,D / CP 0xF1 / JR NZ,orig / LD A,E / CP 0x10 / JR NZ,orig
            // LD H,0 / LD L,C / INC L / RET / JP orig
            const uint8_t stub[] = {
                0x7A, 0xFE, (uint8_t)(HDD_XLT >> 8), 0x20, 0x0A,
                0x7B, 0xFE, (uint8_t)(HDD_XLT & 0xFF), 0x20, 0x05,
                0x26, 0x00, 0x69, 0x2C, 0xC9,
                0xC3, 0x00, 0x00
            };
            for (size_t i = 0; i < sizeof(stub); ++i) ram[HDD_SECTRAN + i] = stub[i];
            if (orig) wr16(ram, (uint16_t)(HDD_SECTRAN + 16), orig);
            ram[jmp_sectran + 0] = 0xC3;
            wr16(ram, (uint16_t)(jmp_sectran + 1), HDD_SECTRAN);
        }
    }

    int patched = 0;
    for (uint8_t d = 0; d < MAX_DRIVES; ++d) {
        if (!(drive_mask & (1u << d))) continue;
        const uint16_t dph = (uint16_t)(dph0 + d * 16);
        const uint16_t alv = (uint16_t)(HDD_ALV0 + d * 256);
        for (int i = 0; i < 256; ++i) ram[alv + i] = 0;
        // Standard DPH: +0 XLT, +8 DIRBUF (keep), +10 DPB, +12 CSV, +14 ALV.
        wr16(ram, dph, HDD_XLT);
        wr16(ram, (uint16_t)(dph + 10), HDD_DPB);
        wr16(ram, (uint16_t)(dph + 12), 0);
        wr16(ram, (uint16_t)(dph + 14), alv);
        patched++;
    }
    Serial.printf("[cpm] HDD DPH=0x%04X XLT=0x%04X DPB=0x%04X ALV=0x%04X\r\n",
                  (unsigned)dph0, (unsigned)HDD_XLT, (unsigned)HDD_DPB,
                  (unsigned)HDD_ALV0);
    return patched;
}

bool AltairBios::handleOut(z80* z, uint8_t port) {
    switch (port) {
        case 0xC0:  // pSELDSK : C = drive
            curDrive_ = z->c;
            return true;

        case 0xC1: {  // pSETTRK : C = track; BC = 16-bit only on HDD
            DiskImage* img = (curDrive_ < MAX_DRIVES) ? drives_[curDrive_] : nullptr;
            if (img && img->isOpen() && img->sectorsPerTrack() > 26)
                curTrack_ = ((uint16_t)z->b << 8) | z->c;
            else
                curTrack_ = z->c;
            return true;
        }

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
    if (!d->readSector(curTrack_, curSector_, sec)) {
        logIoFail("READ", curDrive_, curTrack_, curSector_);
        return false;
    }
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
    if (!d->writeSector(curTrack_, curSector_, sec)) {
        logIoFail("WRITE", curDrive_, curTrack_, curSector_);
        return false;
    }
    return true;
}
