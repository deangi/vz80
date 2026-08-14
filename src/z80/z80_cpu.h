#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

extern "C" {
#include "z80.h"
}

class AltairBios;  // forward decl

// Wrapper around superzazu/z80 that owns a 64KB RAM array and routes console
// I/O to FreeRTOS stream buffers. Ports modelled (matching deramp's CP/M 2.2
// BIOS and standard Altair 8800 hardware):
//
//   88-SIO single-board (TTY, IOBYTE=00):
//     IN  A,(0x00)  status, bits INVERTED:  bit0=0 rx ready, bit7=0 tx ready
//     IN  A,(0x01)  rx data
//     OUT (0x01),A  tx data
//
//   88-2SIO port 1 (CRT, IOBYTE=01):
//     IN  A,(0x10)  status, NON-inverted:   bit0=1 rx ready, bit1=1 tx ready
//     IN  A,(0x11)  rx data
//     OUT (0x11),A  tx data
//
//   Altair iCOM 3712 disk PROM trap ports (set by AltairBios::installStubs):
//     OUT (0xC0..0xC5)  SELDSK / SETTRK / SETSEC / SETDMA / READ / WRITE
//
//   Line printer (88-LPC / deramp LST: LPT):
//     IN  A,(0x02)  status — bit1=1 ready (matches guest LISTST AND 02h)
//     OUT (0x03),A  data   — 7-bit char into lp_capture FIFO → /LPn.TXT
//
// Both SIO ports share the same console stream buffers - the CP/M IOBYTE
// determines which the BIOS talks to. Host also overlays LIST/LISTST jump
// table entries so LST: always uses the LPC ports (see AltairBios).

class Z80CPU {
public:
    static constexpr size_t  RAM_SIZE = 65536;

    // 88-SIO (inverted)
    static constexpr uint8_t PORT_SIO_STATUS  = 0x00;
    static constexpr uint8_t PORT_SIO_DATA    = 0x01;
    // 88-LPC (Okidata / deramp LPT)
    static constexpr uint8_t PORT_LPC_STATUS  = 0x02;
    static constexpr uint8_t PORT_LPC_DATA    = 0x03;
    // 88-2SIO port 1 (non-inverted)
    static constexpr uint8_t PORT_2SIO_STATUS = 0x10;
    static constexpr uint8_t PORT_2SIO_DATA   = 0x11;

    void begin(StreamBufferHandle_t txOut, StreamBufferHandle_t rxIn);
    void reset(uint16_t entryPC = 0x0100);
    void loadProgram(uint16_t addr, const uint8_t* data, size_t len);
    void writeMem(uint16_t addr, uint8_t value) { ram_[addr] = value; }
    uint8_t readMem(uint16_t addr) const        { return ram_[addr]; }

    // Attach the iCOM 3712 disk PROM emulator. nullptr disables it.
    void setBios(AltairBios* bios) { bios_ = bios; }
    AltairBios* bios() { return bios_; }

    // Execute at least `minCycles` worth of T-states; may overshoot by one
    // instruction. Returns number of cycles actually consumed.
    unsigned long runCycles(unsigned long minCycles);
    void step() { z80_step(&cpu_); }

    z80*     cpu()  { return &cpu_; }
    uint8_t* ram()  { return ram_;  }

private:
    z80      cpu_{};
    uint8_t* ram_ = nullptr;     // RAM_SIZE bytes, allocated in begin()
    StreamBufferHandle_t txOut_ = nullptr;
    StreamBufferHandle_t rxIn_  = nullptr;
    AltairBios*          bios_  = nullptr;

    static uint8_t s_read (void* ud, uint16_t addr);
    static void    s_write(void* ud, uint16_t addr, uint8_t val);
    static uint8_t s_in   (z80* z,   uint8_t port);
    static void    s_out  (z80* z,   uint8_t port, uint8_t val);
};
