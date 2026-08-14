#pragma once
#include <Arduino.h>
extern "C" {
#include "../z80/z80.h"
}
#include "../storage/disk_image.h"

// AltairBios - emulates the iCOM 3712 boot PROM jump table the CP/M 2.2 BIOS
// (BIOS.ASM) forwards disk operations to. Disk-only: console I/O is handled
// directly by the BIOS through the 88-SIO/88-2SIO ports (see Z80CPU s_in/s_out).
//
// PROM jump table entries used by the boot loader and BIOS:
//   0xF02B  pSELDSK   C = drive number (0..n)
//   0xF02E  pSETTRK   C = track number
//   0xF031  pSETSEC   C = sector number (1..NUMSEC)
//   0xF034  pSETDMA   BC = DMA address
//   0xF037  pREAD     read sector to DMA addr; Z=success
//   0xF03A  pWRITE    write sector from DMA addr; Z=success
//
// We install at each entry a 3-byte trap stub: OUT (port), A ; RET.
// Cold boot may first load an optional SD PROM image ([system] prom= /
// prom_addr= in z80config.ini); installStubs() then overlays these 18 bytes
// so SD .dsk I/O still works. AltairBios::handleOut() is called by
// Z80CPU::s_out for trap ports 0xC0..0xC5 and performs the requested disk
// operation, setting z->zf for read/write.
//
// SETTRK currently uses C only (8-bit track). An 8 MB CP/M HDD needs
// SETTRK = ((B<<8)|C) plus a guest DPB — see src/cpm/hdd8mb.md.
//
// After CP/M is resident, installListStubs() writes LIST/LISTST routines that
// drive 88-LPC ports 02h/03h and patches the BIOS jump table so LST: always
// uses the host line-printer capture (regardless of IOBYTE).

class AltairBios {
public:
    static constexpr uint8_t  TRAP_PORT_BASE = 0xC0;  // 0xC0..0xC5
    static constexpr uint16_t PROM_BASE      = 0xF02B;
    // LIST / LISTST code lives just above the disk PROM traps.
    static constexpr uint16_t LIST_STUB_BASE = 0xF040;

    enum DriveLetter : uint8_t { A = 0, B = 1, C = 2, D = 3, MAX_DRIVES = 4 };

    bool mount(uint8_t drive, DiskImage* image);
    void unmount(uint8_t drive);
    DiskImage* drive(uint8_t d) { return d < MAX_DRIVES ? drives_[d] : nullptr; }

    // Install the 6 trap stubs into the Z80 RAM at PROM_BASE.
    // ram[] must be the 64KB Z80 memory array.
    void installStubs(uint8_t* ram);

    // Install LIST/LISTST (88-LPC) and patch BIOS JMP LIST / JMP LISTST.
    // Safe to call once page-0 WBOOT vector is valid (after CP/M boots).
    // Returns true if the jump table was patched.
    bool installListStubs(uint8_t* ram);

    // Called from Z80CPU::s_out for trap ports 0xC0..0xC5. Returns true if the
    // port was handled (disk op).
    bool handleOut(z80* z, uint8_t port);

    // Reset selection state (call on cold boot).
    void resetState();

private:
    DiskImage* drives_[MAX_DRIVES] = { nullptr, nullptr, nullptr, nullptr };

    uint8_t  curDrive_  = 0;
    uint16_t curTrack_  = 0;
    uint8_t  curSector_ = 1;
    uint16_t dmaAddr_   = 0x0080;

    bool doRead (z80* z);
    bool doWrite(z80* z);
};
