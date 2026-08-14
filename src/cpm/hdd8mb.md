# 8 MB CP/M HDD BIOS notes

There is no drop-in FDC+ / HDSK binary in this tree, and no `BIOS.ASM`
source. Guest BIOS lives on the boot `.dsk` system tracks (typically 0–1
of `/altair48k-boot.dsk`). The host only emulates the iCOM PROM jump table
(`0xF02B` SELDSK..WRITE → ports `0xC0..0xC5`).

## Reference: porting a CBIOS

Two solid walkthroughs (same basic recipe: stock CCP/BDOS + custom CBIOS):

1. [cpuville — Setting up CP/M 2.2 on a New Z80 Computer](http://cpuville.com/Code/CPM-on-a-new-computer.html)
   (Donn Steward) — skeletal BIOS from the Alteration Guide, IDE LBA, format/putsys.
2. [ciernioo — CP/M 2.2 Up And Running](https://ciernioo.wordpress.com/2016/05/11/cpm-2-2-up-and-running/)
   — follows Steward; uses Grant Searle’s TASM-friendly `cpm22.asm`
   ([Breadboard CP/M](http://searle.x10host.com/cpm/index.html) / Clark Calkins
   disassembly). Same “stash SET* / do work in READ+WRITE” pattern on CF,
   with `format` + `putsys` for system tracks. Keeps the stock four ~250 KB
   floppy DPBs rather than growing one drive to 8 MB.

Useful points for vZ80:

- **SELDSK / SETTRK / SETSEC / SETDMA** only stash state; **READ / WRITE**
  do I/O. That matches this host: guest CBIOS jumps to the iCOM PROM table,
  and `AltairBios` performs the SD sector transfer.
- Disk parameter tables need not match physical media as long as READ/WRITE
  map each logical (drive, track, sector) to a unique image offset. Both
  articles keep IBM 8" floppy DPBs and map A:–D: onto CF/IDE LBA. Here we
  can either keep floppy geometry per `.dsk`, or add a true 8 MB DPB (below)
  for a `.hdd`.
- Their LIST/PUNCH/READER stubs are often no-ops (one serial console). vZ80
  instead implements 88-LPC `02h`/`03h` → `/LPn.TXT` and patches guest
  LIST/LISTST (see `list_lpc.asm`).
- Sources: Clark Calkins `CPM22.Z80` / Searle `cpm22.asm` (CCP+BDOS),
  skeletal BIOS from the CP/M 2.2 Alteration Guide (8080 → Z80).

Their IDE/CF ports and LBA packing are **not** what this emulator
implements. Keep calling the iCOM PROM traps; only change guest DPBs and
host SETTRK width / image size.

## Required guest DPB (fork iCOM `BIOS.ASM`)

| Field | Value | Meaning |
|-------|-------|---------|
| SPT   | 32    | 128-byte sectors per track |
| BSH   | 5     | block shift (4 KiB) |
| BLM   | 31    | block mask |
| EXM   | 1     | extent mask (4 KiB, DSM>255) |
| DSM   | 2045  | last alloc block (0..2045) |
| DRM   | 511   | 512 directory entries |
| AL0   | 0xF0  | first 4 blocks reserved for directory |
| AL1   | 0x00  | |
| CKS   | 0     | non-removable (no dir checksum) |
| OFF   | 2     | reserved tracks |

CP/M 2.2 max allocation is **DSM ≤ 2045** at 4 KiB blocks → **8,192,000
data bytes**. With 2 reserved tracks that is an **8,388,608-byte** image
(2048 tracks × 32 sectors × 128 bytes), filled with `0xE5`.

`SETTRK` must pass a **16-bit** track in BC (`((B<<8)|C)`). Floppy BIOS
only uses C, so the stock iCOM image cannot address 2048 tracks.

Suggested host image: `/cpm8mb.hdd`, 8,388,608 bytes, `0xE5` fill.
`DiskImage` already accepts custom track/spt; `AltairBios::handleOut`
SETTRK still stores `z->c` only and must be changed before mounting HDD.

Do **not** reuse FDC+ / Altair HDSK binaries unchanged: those talk to
different controller ports, not the iCOM PROM traps this host implements.
