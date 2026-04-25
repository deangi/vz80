#pragma once
#include <Arduino.h>

// Minimal Z80 program used by M3 to prove the emulator -> stream -> console
// path works end-to-end. Prints "Hello from Z80! " forever with a short delay
// between repeats. Designed to load at 0x0100.
//
// Assembly:
//   ORG 0100H
//   START:
//     LD   HL, msg
//     LD   B,  msg_len
//   OUTL:
//     LD   A,  (HL)
//     OUT  (01H), A
//     INC  HL
//     DJNZ OUTL
//     LD   BC, 0FFFFH
//   DEL:
//     DEC  BC
//     LD   A,  B
//     OR   C
//     JR   NZ, DEL
//     JP   START
//   msg: DB CR, LF, "Hello from Z80! "

static const uint16_t HELLO_PROG_ORIGIN = 0x0100;

static const uint8_t HELLO_PROG[] = {
    0x21, 0x16, 0x01,            // 0100: LD HL, 0x0116
    0x06, 0x12,                  // 0103: LD B, 18
    0x7E,                        // 0105: LD A, (HL)
    0xD3, 0x01,                  // 0106: OUT (0x01), A
    0x23,                        // 0108: INC HL
    0x10, 0xFA,                  // 0109: DJNZ -6  -> 0x0105
    0x01, 0xFF, 0xFF,            // 010B: LD BC, 0xFFFF
    0x0B,                        // 010E: DEC BC
    0x78,                        // 010F: LD A, B
    0xB1,                        // 0110: OR C
    0x20, 0xFB,                  // 0111: JR NZ, -5 -> 0x010E
    0xC3, 0x00, 0x01,            // 0113: JP 0x0100
    // 0116: msg, 18 bytes: CR LF "Hello from Z80! "
    0x0D, 0x0A,
    'H','e','l','l','o',' ','f','r','o','m',' ','Z','8','0','!',' ',
};
