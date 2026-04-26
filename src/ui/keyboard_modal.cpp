#include "keyboard_modal.h"
#include <Arduino.h>

namespace {

constexpr int KBD_LEFT = 0;
constexpr int KW       = 32;       // 10 keys × 32 = 320 (full screen width)
constexpr int KH       = 24;

enum KeyKind : uint8_t { KK_Char, KK_Shift, KK_Ctrl, KK_Special };

struct Key {
    int16_t  x, y;
    uint16_t w, h;
    uint8_t  base;     // unshifted byte; 0 for modifier keys
    uint8_t  shifted;  // shifted byte; 0 if same as base
    const char* label;
    KeyKind  kind;
};

#define ROW(n) (KeyboardModal::KBD_Y0 + (n) * KH)

static const Key kKeys[] = {
    // Row 0: digits (10)
    { KBD_LEFT + 0*KW,  ROW(0), KW, KH, '1', '!', "1", KK_Char },
    { KBD_LEFT + 1*KW,  ROW(0), KW, KH, '2', '@', "2", KK_Char },
    { KBD_LEFT + 2*KW,  ROW(0), KW, KH, '3', '#', "3", KK_Char },
    { KBD_LEFT + 3*KW,  ROW(0), KW, KH, '4', '$', "4", KK_Char },
    { KBD_LEFT + 4*KW,  ROW(0), KW, KH, '5', '%', "5", KK_Char },
    { KBD_LEFT + 5*KW,  ROW(0), KW, KH, '6', '^', "6", KK_Char },
    { KBD_LEFT + 6*KW,  ROW(0), KW, KH, '7', '&', "7", KK_Char },
    { KBD_LEFT + 7*KW,  ROW(0), KW, KH, '8', '*', "8", KK_Char },
    { KBD_LEFT + 8*KW,  ROW(0), KW, KH, '9', '(', "9", KK_Char },
    { KBD_LEFT + 9*KW,  ROW(0), KW, KH, '0', ')', "0", KK_Char },

    // Row 1: QWERTYUIOP (10)
    { KBD_LEFT + 0*KW,  ROW(1), KW, KH, 'q', 'Q', "q", KK_Char },
    { KBD_LEFT + 1*KW,  ROW(1), KW, KH, 'w', 'W', "w", KK_Char },
    { KBD_LEFT + 2*KW,  ROW(1), KW, KH, 'e', 'E', "e", KK_Char },
    { KBD_LEFT + 3*KW,  ROW(1), KW, KH, 'r', 'R', "r", KK_Char },
    { KBD_LEFT + 4*KW,  ROW(1), KW, KH, 't', 'T', "t", KK_Char },
    { KBD_LEFT + 5*KW,  ROW(1), KW, KH, 'y', 'Y', "y", KK_Char },
    { KBD_LEFT + 6*KW,  ROW(1), KW, KH, 'u', 'U', "u", KK_Char },
    { KBD_LEFT + 7*KW,  ROW(1), KW, KH, 'i', 'I', "i", KK_Char },
    { KBD_LEFT + 8*KW,  ROW(1), KW, KH, 'o', 'O', "o", KK_Char },
    { KBD_LEFT + 9*KW,  ROW(1), KW, KH, 'p', 'P', "p", KK_Char },

    // Row 2: ASDFGHJKL + ENT
    { KBD_LEFT + 0*KW,  ROW(2), KW, KH, 'a', 'A', "a", KK_Char },
    { KBD_LEFT + 1*KW,  ROW(2), KW, KH, 's', 'S', "s", KK_Char },
    { KBD_LEFT + 2*KW,  ROW(2), KW, KH, 'd', 'D', "d", KK_Char },
    { KBD_LEFT + 3*KW,  ROW(2), KW, KH, 'f', 'F', "f", KK_Char },
    { KBD_LEFT + 4*KW,  ROW(2), KW, KH, 'g', 'G', "g", KK_Char },
    { KBD_LEFT + 5*KW,  ROW(2), KW, KH, 'h', 'H', "h", KK_Char },
    { KBD_LEFT + 6*KW,  ROW(2), KW, KH, 'j', 'J', "j", KK_Char },
    { KBD_LEFT + 7*KW,  ROW(2), KW, KH, 'k', 'K', "k", KK_Char },
    { KBD_LEFT + 8*KW,  ROW(2), KW, KH, 'l', 'L', "l", KK_Char },
    { KBD_LEFT + 9*KW,  ROW(2), KW, KH, '\r', '\r', "ENT", KK_Special },

    // Row 3: ZXCVBNM,. + BS
    { KBD_LEFT + 0*KW,  ROW(3), KW, KH, 'z', 'Z', "z", KK_Char },
    { KBD_LEFT + 1*KW,  ROW(3), KW, KH, 'x', 'X', "x", KK_Char },
    { KBD_LEFT + 2*KW,  ROW(3), KW, KH, 'c', 'C', "c", KK_Char },
    { KBD_LEFT + 3*KW,  ROW(3), KW, KH, 'v', 'V', "v", KK_Char },
    { KBD_LEFT + 4*KW,  ROW(3), KW, KH, 'b', 'B', "b", KK_Char },
    { KBD_LEFT + 5*KW,  ROW(3), KW, KH, 'n', 'N', "n", KK_Char },
    { KBD_LEFT + 6*KW,  ROW(3), KW, KH, 'm', 'M', "m", KK_Char },
    { KBD_LEFT + 7*KW,  ROW(3), KW, KH, ',', '<', ",", KK_Char },
    { KBD_LEFT + 8*KW,  ROW(3), KW, KH, '.', '>', ".", KK_Char },
    { KBD_LEFT + 9*KW,  ROW(3), KW, KH, 0x08, 0x08, "BS", KK_Special },

    // Row 4: modifiers + space + extras (totals 320 across the full screen)
    { KBD_LEFT + 0,     ROW(4), 48,  KH, 0,    0,    "SHF",   KK_Shift   },
    { KBD_LEFT + 48,    ROW(4), 48,  KH, 0,    0,    "CTL",   KK_Ctrl    },
    { KBD_LEFT + 96,    ROW(4), 128, KH, ' ',  ' ',  "SPACE", KK_Special },
    { KBD_LEFT + 224,   ROW(4), 48,  KH, 0x1B, 0x1B, "ESC",   KK_Special },
    { KBD_LEFT + 272,   ROW(4), 48,  KH, '\t', '\t', "TAB",   KK_Special },
};

static constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

}  // namespace

bool KeyboardModal::open(lgfx::LGFX_Device* lcd) {
    lcd_ = lcd;
    prevTouch_  = true;     // ignore the touch that triggered the open
    shift_      = false;
    ctrl_       = false;
    pressedKey_ = -1;
    lastByte_   = 0;
    render();
    return true;
}

void KeyboardModal::close() { lcd_ = nullptr; }

void KeyboardModal::drawKey(int idx, bool pressed) {
    if (!lcd_ || idx < 0 || idx >= kKeyCount) return;
    const Key& k = kKeys[idx];
    bool armed = (k.kind == KK_Shift && shift_) ||
                 (k.kind == KK_Ctrl  && ctrl_);
    uint16_t bg;
    if (pressed)      bg = TFT_ORANGE;
    else if (armed)   bg = TFT_DARKGREEN;
    else              bg = 0x2104;  // dark slate
    uint16_t fg = TFT_WHITE;

    lcd_->fillRect(k.x + 1, k.y + 1, k.w - 2, k.h - 2, bg);
    lcd_->drawRect(k.x, k.y, k.w, k.h, TFT_DARKGREY);

    lcd_->setTextDatum(middle_center);
    lcd_->setTextColor(fg, bg);
    lcd_->setTextSize(k.kind == KK_Char ? 2 : 1);
    lcd_->drawString(k.label, k.x + k.w / 2, k.y + k.h / 2);
    lcd_->setTextDatum(top_left);
    lcd_->setTextSize(1);
}

void KeyboardModal::render() {
    if (!lcd_) return;
    // Wipe the whole keyboard area, then draw every key.
    lcd_->fillRect(0, KBD_Y0, 320, KBD_H, TFT_BLACK);
    for (int i = 0; i < kKeyCount; ++i) drawKey(i, false);
}

int KeyboardModal::hitTest(int x, int y) const {
    for (int i = 0; i < kKeyCount; ++i) {
        const Key& k = kKeys[i];
        if (x >= k.x && x < k.x + k.w &&
            y >= k.y && y < k.y + k.h) return i;
    }
    return -1;
}

uint8_t KeyboardModal::resolveKey(int idx) const {
    const Key& k = kKeys[idx];
    uint8_t b = (shift_ && k.shifted) ? k.shifted : k.base;
    if (ctrl_) {
        // Ctrl-letter -> 0x01..0x1A. Apply on the resolved (possibly
        // shifted) byte so Ctrl+SHFT+A and Ctrl+A behave identically.
        if (b >= 'a' && b <= 'z') b = (b - 'a') + 1;
        else if (b >= 'A' && b <= 'Z') b = (b - 'A') + 1;
    }
    return b;
}

KeyboardModal::Result KeyboardModal::poll() {
    if (!lcd_) return Result::CONTINUE;
    int32_t tx = 0, ty = 0;
    bool touched = (lcd_->getTouch(&tx, &ty) != 0);

    Result r = Result::CONTINUE;

    if (touched && !prevTouch_) {
        int idx = hitTest(tx, ty);
        if (idx >= 0) {
            const Key& k = kKeys[idx];
            pressedKey_ = idx;
            pressedAt_  = millis();
            drawKey(idx, true);

            if (k.kind == KK_Shift) {
                shift_ = !shift_;
                drawKey(idx, true);   // refresh in case armed-color matters
            } else if (k.kind == KK_Ctrl) {
                ctrl_ = !ctrl_;
                drawKey(idx, true);
            } else {
                // Char or Special — emit a byte, then auto-release modifiers.
                lastByte_ = resolveKey(idx);
                r = Result::KEY;
                bool needRedraw = shift_ || ctrl_;
                shift_ = false;
                ctrl_  = false;
                if (needRedraw) {
                    // Redraw the modifier keys to clear their armed highlight.
                    for (int i = 0; i < kKeyCount; ++i) {
                        if (kKeys[i].kind == KK_Shift || kKeys[i].kind == KK_Ctrl) {
                            drawKey(i, false);
                        }
                    }
                }
            }
        }
    }

    // Visual press release after ~120 ms (matches top-strip buttons).
    if (pressedKey_ >= 0 && (millis() - pressedAt_) > 120) {
        int idx = pressedKey_;
        pressedKey_ = -1;
        drawKey(idx, false);
    }

    prevTouch_ = touched;
    return r;
}
