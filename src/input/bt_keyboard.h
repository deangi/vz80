#pragma once
// Bluetooth HID-host keyboard driver (M6).
//
// Accepts keystrokes from either a BR/EDR (Classic) or BLE HID keyboard via
// ESP-IDF's unified esp_hidh API. Decoded ASCII bytes are pushed into the
// StreamBufferHandle that connects keyboard -> Z80 (same buffer the Serial
// bridge uses, so both input paths coexist).
//
// Usage:
//   BtKeyboard btKbd;
//   btKbd.begin(rxStream);        // init stack, auto-reconnect to bonded kbd
//   btKbd.startPairing();         // triggered by UI BT button, 20s window
//
// Scope of this first cut:
//   - Dual-mode (BTDM) init so one firmware handles both transports.
//   - HID Boot Protocol keyboard report (8 bytes: mods + 6 keycodes).
//   - Modifier-aware ASCII for US QWERTY; Ctrl-letter produces 0x01..0x1A.
//   - Arrow keys / function keys emit no bytes for now (CP/M doesn't need
//     them; WordStar users can remap later).
//   - No bonding persistence yet: after reboot, re-pair via BT button.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

class BtKeyboard {
public:
    enum class State : uint8_t {
        Off,          // begin() not called yet or failed
        Idle,         // stack up, nothing connected
        Pairing,      // discovery/scan window open
        Connecting,   // found a candidate, HID open in progress
        Connected,    // HID open, receiving reports
    };

    bool  begin(StreamBufferHandle_t rx);
    void  startPairing();                 // open 20 s discovery window
    void  disconnect();

    State       state()      const { return state_; }
    const char* statusText() const;       // short label for UI ("BT", "BT?", "BT+")

    // Called by the internal HID callback; public because C callbacks
    // need to reach in. Not for normal user code.
    void onHidInput(const uint8_t* data, uint16_t len);
    void onHidOpen(bool ok, const char* dev_name);
    void onHidClose();
    void onPairingTimeout();

private:
    void pushChar(uint8_t c);

    StreamBufferHandle_t rx_        = nullptr;
    State                state_     = State::Off;
    uint8_t              prevKeys_[6] = {0};
    uint8_t              prevMods_  = 0;
    char                 devName_[32] = {0};
};

extern BtKeyboard gBtKbd;  // single instance; C callbacks reach it via this
