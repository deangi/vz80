#include "keyboard_modal.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

namespace {

constexpr int KBD_LEFT = 0;
constexpr int KW = 32;
constexpr int KH = 24;

enum KeyKind : uint8_t { KK_Char, KK_Shift, KK_Ctrl, KK_Special, KK_Close };

struct Key {
  int16_t x, y;
  uint16_t w, h;
  uint8_t base;
  uint8_t shifted;
  const char* label;
  KeyKind kind;
};

#define ROW(n) (KeyboardModal::KBD_Y0 + (n) * KH)

static const Key kKeys[] = {
  { KBD_LEFT + 0*KW, ROW(0), KW, KH, '1', '!', "1", KK_Char },
  { KBD_LEFT + 1*KW, ROW(0), KW, KH, '2', '@', "2", KK_Char },
  { KBD_LEFT + 2*KW, ROW(0), KW, KH, '3', '#', "3", KK_Char },
  { KBD_LEFT + 3*KW, ROW(0), KW, KH, '4', '$', "4", KK_Char },
  { KBD_LEFT + 4*KW, ROW(0), KW, KH, '5', '%', "5", KK_Char },
  { KBD_LEFT + 5*KW, ROW(0), KW, KH, '6', '^', "6", KK_Char },
  { KBD_LEFT + 6*KW, ROW(0), KW, KH, '7', '&', "7", KK_Char },
  { KBD_LEFT + 7*KW, ROW(0), KW, KH, '8', '*', "8", KK_Char },
  { KBD_LEFT + 8*KW, ROW(0), KW, KH, '9', '(', "9", KK_Char },
  { KBD_LEFT + 9*KW, ROW(0), KW, KH, '0', ')', "0", KK_Char },

  { KBD_LEFT + 0*KW, ROW(1), KW, KH, 'q', 'Q', "q", KK_Char },
  { KBD_LEFT + 1*KW, ROW(1), KW, KH, 'w', 'W', "w", KK_Char },
  { KBD_LEFT + 2*KW, ROW(1), KW, KH, 'e', 'E', "e", KK_Char },
  { KBD_LEFT + 3*KW, ROW(1), KW, KH, 'r', 'R', "r", KK_Char },
  { KBD_LEFT + 4*KW, ROW(1), KW, KH, 't', 'T', "t", KK_Char },
  { KBD_LEFT + 5*KW, ROW(1), KW, KH, 'y', 'Y', "y", KK_Char },
  { KBD_LEFT + 6*KW, ROW(1), KW, KH, 'u', 'U', "u", KK_Char },
  { KBD_LEFT + 7*KW, ROW(1), KW, KH, 'i', 'I', "i", KK_Char },
  { KBD_LEFT + 8*KW, ROW(1), KW, KH, 'o', 'O', "o", KK_Char },
  { KBD_LEFT + 9*KW, ROW(1), KW, KH, 'p', 'P', "p", KK_Char },

  { KBD_LEFT + 0*KW, ROW(2), KW, KH, 'a', 'A', "a", KK_Char },
  { KBD_LEFT + 1*KW, ROW(2), KW, KH, 's', 'S', "s", KK_Char },
  { KBD_LEFT + 2*KW, ROW(2), KW, KH, 'd', 'D', "d", KK_Char },
  { KBD_LEFT + 3*KW, ROW(2), KW, KH, 'f', 'F', "f", KK_Char },
  { KBD_LEFT + 4*KW, ROW(2), KW, KH, 'g', 'G', "g", KK_Char },
  { KBD_LEFT + 5*KW, ROW(2), KW, KH, 'h', 'H', "h", KK_Char },
  { KBD_LEFT + 6*KW, ROW(2), KW, KH, 'j', 'J', "j", KK_Char },
  { KBD_LEFT + 7*KW, ROW(2), KW, KH, 'k', 'K', "k", KK_Char },
  { KBD_LEFT + 8*KW, ROW(2), KW, KH, 'l', 'L', "l", KK_Char },
  { KBD_LEFT + 9*KW, ROW(2), KW, KH, '\r', '\r', "ENT", KK_Special },

  { KBD_LEFT + 0*KW, ROW(3), KW, KH, 'z', 'Z', "z", KK_Char },
  { KBD_LEFT + 1*KW, ROW(3), KW, KH, 'x', 'X', "x", KK_Char },
  { KBD_LEFT + 2*KW, ROW(3), KW, KH, 'c', 'C', "c", KK_Char },
  { KBD_LEFT + 3*KW, ROW(3), KW, KH, 'v', 'V', "v", KK_Char },
  { KBD_LEFT + 4*KW, ROW(3), KW, KH, 'b', 'B', "b", KK_Char },
  { KBD_LEFT + 5*KW, ROW(3), KW, KH, 'n', 'N', "n", KK_Char },
  { KBD_LEFT + 6*KW, ROW(3), KW, KH, 'm', 'M', "m", KK_Char },
  { KBD_LEFT + 7*KW, ROW(3), KW, KH, ',', '<', ",", KK_Char },
  { KBD_LEFT + 8*KW, ROW(3), KW, KH, '.', '>', ".", KK_Char },
  { KBD_LEFT + 9*KW, ROW(3), KW, KH, 0x08, 0x08, "BS", KK_Special },

  { KBD_LEFT + 0,   ROW(4), 48,  KH, 0,    0,    "SHF",   KK_Shift },
  { KBD_LEFT + 48,  ROW(4), 48,  KH, 0,    0,    "CTL",   KK_Ctrl },
  { KBD_LEFT + 96,  ROW(4), 128, KH, ' ',  ' ',  "SPACE", KK_Special },
  { KBD_LEFT + 224, ROW(4), 48,  KH, 0x1B, 0x1B, "ESC",   KK_Special },
  { KBD_LEFT + 272, ROW(4), 48,  KH, '\t', '\t', "TAB",   KK_Special },

  { KBD_LEFT + 0,   ROW(5), 224, KH, 0, 0, "keyboard", KK_Special },
  { KBD_LEFT + 224, ROW(5), 96,  KH, 0, 0, "CLOSE", KK_Close },
};

constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

}  // namespace

void KeyboardModal::open() {
  open_ = true;
  shift_ = false;
  ctrl_ = false;
  dirty_ = true;
}

void KeyboardModal::close() {
  open_ = false;
  shift_ = false;
  ctrl_ = false;
  dirty_ = true;
}

void KeyboardModal::draw(TFT_eSPI& tft) {
  if (!open_ || !dirty_) return;
  dirty_ = false;

  tft.fillRect(0, KBD_Y0, 320, KBD_H, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  for (int i = 0; i < kKeyCount; i++) {
    const Key& k = kKeys[i];
    bool armed = (k.kind == KK_Shift && shift_) || (k.kind == KK_Ctrl && ctrl_);
    uint16_t bg = armed ? TFT_DARKGREEN : 0x2104;
    if (k.kind == KK_Close) bg = 0x7800;
    if (i == kKeyCount - 2) bg = TFT_BLACK;

    tft.fillRect(k.x + 1, k.y + 1, k.w - 2, k.h - 2, bg);
    if (i != kKeyCount - 2) tft.drawRect(k.x, k.y, k.w, k.h, TFT_DARKGREY);

    tft.setTextColor(i == kKeyCount - 2 ? TFT_DARKGREY : TFT_WHITE, bg);
    tft.setTextSize(k.kind == KK_Char ? 2 : 1);
    tft.drawString(k.label, k.x + k.w / 2, k.y + k.h / 2);
  }

  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
}

int KeyboardModal::hitTest(int x, int y) const {
  for (int i = 0; i < kKeyCount; i++) {
    const Key& k = kKeys[i];
    if (x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h)
      return i;
  }
  return -1;
}

uint8_t KeyboardModal::resolveKey(int idx) const {
  const Key& k = kKeys[idx];
  uint8_t b = (shift_ && k.shifted) ? k.shifted : k.base;
  if (ctrl_) {
    if (b >= 'a' && b <= 'z') b = (b - 'a') + 1;
    else if (b >= 'A' && b <= 'Z') b = (b - 'A') + 1;
  }
  return b;
}

KeyboardModal::Result KeyboardModal::handleTap(int x, int y, uint8_t* out) {
  if (!open_) return Result::NONE;
  int idx = hitTest(x, y);
  if (idx < 0) return Result::NONE;

  const Key& k = kKeys[idx];
  if (k.kind == KK_Close) return Result::CLOSE;
  if (idx == kKeyCount - 2) return Result::NONE;

  if (k.kind == KK_Shift) {
    shift_ = !shift_;
    dirty_ = true;
    return Result::NONE;
  }
  if (k.kind == KK_Ctrl) {
    ctrl_ = !ctrl_;
    dirty_ = true;
    return Result::NONE;
  }

  if (out) *out = resolveKey(idx);
  shift_ = false;
  ctrl_ = false;
  dirty_ = true;
  return Result::KEY;
}
