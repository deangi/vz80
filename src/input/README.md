# input/ — (intentionally empty)

The original M6 plan was a Bluetooth HID keyboard host. That was
removed because the original ESP32 in the CYD2USB doesn't have enough
internal DRAM to run WiFi and Bluedroid simultaneously, and there's no
PSRAM on this board to relocate the 64 KB Z80 RAM out of internal DRAM.
See `vZ80.ino` (top-of-file note) and `CLAUDE.md` for full details.

Current keyboard input paths:
- **Telnet over WiFi** (M7) — `src/network/telnet_server.{h,cpp}`
- **On-screen keyboard** (M8) — `src/ui/keyboard_modal.{h,cpp}`
- **USB serial debug bridge** — `drainSerialToZ80()` in `vZ80.ino`
