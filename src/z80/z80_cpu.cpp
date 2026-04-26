#include "z80_cpu.h"
#include "../cpm/altair_bios.h"
#include <string.h>
#include <esp_heap_caps.h>

void Z80CPU::begin(StreamBufferHandle_t txOut, StreamBufferHandle_t rxIn) {
    txOut_ = txOut;
    rxIn_  = rxIn;

    if (!ram_) {
        // Allocate from internal 8-bit RAM; every Z80 mem access hits this,
        // so we don't want PSRAM here even on boards that have it.
        ram_ = (uint8_t*)heap_caps_malloc(RAM_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (!ram_) {
            Serial.printf("[z80] RAM alloc FAIL (%u bytes)\n", (unsigned)RAM_SIZE);
            // Fall back to any 8-bit RAM (PSRAM if present). Slow but boots.
            ram_ = (uint8_t*)heap_caps_malloc(RAM_SIZE, MALLOC_CAP_8BIT);
        }
    }

    z80_init(&cpu_);
    cpu_.userdata   = this;
    cpu_.read_byte  = &Z80CPU::s_read;
    cpu_.write_byte = &Z80CPU::s_write;
    cpu_.port_in    = &Z80CPU::s_in;
    cpu_.port_out   = &Z80CPU::s_out;

    if (ram_) memset(ram_, 0, RAM_SIZE);
    cpu_.pc = 0x0000;
}

void Z80CPU::reset(uint16_t entryPC) {
    void*    ud    = cpu_.userdata;
    auto     rb    = cpu_.read_byte;
    auto     wb    = cpu_.write_byte;
    auto     pi    = cpu_.port_in;
    auto     po    = cpu_.port_out;
    z80_init(&cpu_);
    cpu_.userdata   = ud;
    cpu_.read_byte  = rb;
    cpu_.write_byte = wb;
    cpu_.port_in    = pi;
    cpu_.port_out   = po;
    cpu_.pc         = entryPC;
}

void Z80CPU::loadProgram(uint16_t addr, const uint8_t* data, size_t len) {
    if ((uint32_t)addr + len > RAM_SIZE) len = RAM_SIZE - addr;
    memcpy(&ram_[addr], data, len);
}

unsigned long Z80CPU::runCycles(unsigned long minCycles) {
    unsigned long start = cpu_.cyc;
    while ((cpu_.cyc - start) < minCycles) {
        z80_step(&cpu_);
    }
    return cpu_.cyc - start;
}

// --- static callbacks -------------------------------------------------------

uint8_t Z80CPU::s_read(void* ud, uint16_t addr) {
    return static_cast<Z80CPU*>(ud)->ram_[addr];
}

void Z80CPU::s_write(void* ud, uint16_t addr, uint8_t val) {
    static_cast<Z80CPU*>(ud)->ram_[addr] = val;
}

uint8_t Z80CPU::s_in(z80* z, uint8_t port) {
    auto* self = static_cast<Z80CPU*>(z->userdata);
    bool rxReady = self->rxIn_ &&
                   xStreamBufferBytesAvailable(self->rxIn_) > 0;
    // tx ready only when txOut_ has room — otherwise the Z80 polls until
    // the display task drains a byte. Without this, BDOS CONOUT would
    // silently drop bytes (xStreamBufferSend timeout=0) on bursts.
    bool txReady = self->txOut_ &&
                   xStreamBufferSpacesAvailable(self->txOut_) > 0;

    switch (port) {
        // 88-SIO status: bits inverted. All 1s = idle. Clear bit when ready.
        case PORT_SIO_STATUS: {
            uint8_t s = 0xFF;
            if (txReady) s &= ~0x80;      // tx ready -> bit 7 = 0
            if (rxReady) s &= ~0x01;      // rx ready -> bit 0 = 0
            return s;
        }
        case PORT_SIO_DATA: {
            uint8_t b = 0;
            if (self->rxIn_) xStreamBufferReceive(self->rxIn_, &b, 1, 0);
            return b;
        }

        // 88-2SIO status: non-inverted. 0 = idle. Set bit when ready.
        case PORT_2SIO_STATUS: {
            uint8_t s = 0;
            if (txReady) s |= 0x02;       // tx ready -> bit 1 = 1
            if (rxReady) s |= 0x01;       // rx ready -> bit 0 = 1
            return s;
        }
        case PORT_2SIO_DATA: {
            uint8_t b = 0;
            if (self->rxIn_) xStreamBufferReceive(self->rxIn_, &b, 1, 0);
            return b;
        }

        default:
            return 0xFF;
    }
}

void Z80CPU::s_out(z80* z, uint8_t port, uint8_t val) {
    auto* self = static_cast<Z80CPU*>(z->userdata);

    // Disk trap ports first - 0xC0..0xC5 routed to the iCOM 3712 emulator.
    if (port >= AltairBios::TRAP_PORT_BASE && port <= AltairBios::TRAP_PORT_BASE + 5) {
        if (self->bios_) self->bios_->handleOut(z, port);
        return;
    }
    // (Status register conventions are set inside AltairBios::handleOut —
    // it writes z->a and z->zf so the BIOS can check either.)

    switch (port) {
        case PORT_SIO_DATA:
        case PORT_2SIO_DATA:
            if (self->txOut_) {
                xStreamBufferSend(self->txOut_, &val, 1, 0);
            }
            break;
        // SIO control register writes (port 0/0x10) are init/reset commands -
        // ignore them.
        default:
            break;
    }
}
