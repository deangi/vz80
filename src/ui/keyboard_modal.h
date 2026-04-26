#pragma once
#include <LovyanGFX.hpp>
#include <stdint.h>

// On-screen US-QWERTY keyboard (M8). Modal overlay that occupies the
// bottom 144 px of the LCD (rows 96..239) when open. The console area
// above (rows 48..95, 6 visible rows) shows buffer rows 18..23 so the
// CP/M cursor stays visible while typing — caller is responsible for
// invoking console.setViewRowSpan(18, 6) on open and (0, 24) on close.
//
// Layout (240 px wide, centered with 40 px L/R margins):
//   row 1: 1 2 3 4 5 6 7 8 9 0
//   row 2: Q W E R T Y U I O P
//   row 3: A S D F G H J K L \r        (last key sends CR)
//   row 4: Z X C V B N M , . BS        (last key sends 0x08)
//   row 5: SHFT(40) CTRL(40) SPACE(80) ESC(40) TAB(40)
//
// Modifiers are sticky one-shot: tap SHFT, the next non-modifier key
// emits its shifted form, then SHFT auto-releases. CTRL same: next
// letter emits 0x01..0x1A. Tapping a modifier twice clears it.
//
// poll() must be called repeatedly while open. Each user tap that
// produces a byte sets lastByte() and returns Result::KEY exactly once.

class KeyboardModal {
public:
    enum class Result : uint8_t {
        CONTINUE,     // no key event this poll
        KEY,          // a key was pressed; read via lastByte()
    };

    bool open(lgfx::LGFX_Device* lcd);
    void close();
    bool isOpen() const { return lcd_ != nullptr; }

    Result  poll();
    uint8_t lastByte() const { return lastByte_; }

    // Y of top of keyboard area on the LCD. Caller can clear/redraw
    // the area above (LCD rows 48..95) when closing.
    static constexpr int KBD_Y0 = 96;
    static constexpr int KBD_H  = 144;

private:
    lgfx::LGFX_Device* lcd_       = nullptr;
    bool               prevTouch_ = true;       // ignore held-down on open
    uint8_t            lastByte_  = 0;
    bool               shift_     = false;
    bool               ctrl_      = false;
    int8_t             pressedKey_ = -1;        // index into kKeys, -1 = none
    uint32_t           pressedAt_  = 0;

    void render();
    void drawKey(int idx, bool pressed);
    void drawModifierState();
    int  hitTest(int x, int y) const;
    uint8_t resolveKey(int idx) const;
};
