#include "touch_panel.h"
#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#include <string.h>

static const char* kLabels[TouchPanel::N_BUTTONS] = { "SET", "KBD", "SCR" };

uint16_t TouchPanel::scrollColFor(uint8_t idx) {
    static const uint16_t kCols[4] = { 0, 9, 18, 27 };
    return kCols[idx & 3];
}

bool TouchPanel::begin(lgfx::LGFX_Device* lcd) {
    lcd_ = lcd;
    loadCalibration();
    render();
    return true;
}

void TouchPanel::drawButton(int idx, bool pressed) {
    if (!lcd_ || idx < 0 || idx >= N_BUTTONS) return;
    int x;
    switch (idx) {
        case 0:  x = kSetupX;  break;
        case 1:  x = kKbdX;    break;
        case 2:  x = kScrollX; break;
        default: return;
    }
    int w = BTN_W;

    // Repaint the bar slice behind the button so the bevel edges sit on
    // the green background, not the previous frame's bevel. Stops above
    // the separator+buffer strip (last 4 px of STRIP_H).
    lcd_->fillRect(x, 0, w, STRIP_H - 4, kBarBg);

    uint16_t face = pressed ? TFT_ORANGE : kBtnFace;
    uint16_t fg   = TFT_WHITE;

    // Button body: rounded rect, inset 2 px from edges. Bottom sits at
    // y=41 to clear the separator (y=44..45) and 2-px black buffer (46..47).
    const int bx = x + 2;
    const int by = 2;
    const int bw = w - 4;
    const int bh = STRIP_H - 8;   // y=2..41
    lcd_->fillRoundRect(bx, by, bw, bh, 4, face);

    // Bevel: top/left highlight + bottom/right shadow. Inverted when pressed
    // so the button reads as "pushed in".
    uint16_t hi = pressed ? kBtnSh : kBtnHi;
    uint16_t sh = pressed ? kBtnHi : kBtnSh;
    lcd_->drawFastHLine(bx + 1, by,          bw - 2, hi);
    lcd_->drawFastVLine(bx,     by + 1,      bh - 2, hi);
    lcd_->drawFastHLine(bx + 1, by + bh - 1, bw - 2, sh);
    lcd_->drawFastVLine(bx + bw - 1, by + 1, bh - 2, sh);

    const int cx = x + w / 2;
    const int cy = by + bh / 2;

    switch (idx) {
        case 1:  // KBD — keyboard pictogram
            drawKeyboardIcon(cx, cy, fg);
            break;
        case 2:  // SCROLL — right-facing arrow
            drawScrollArrow(cx, cy, fg);
            break;
        default: // SETUP — keep text label
            lcd_->setTextDatum(middle_center);
            lcd_->setTextColor(fg, face);
            lcd_->setTextSize(2);
            lcd_->drawString(kLabels[idx], cx, cy);
            lcd_->setTextDatum(top_left);
            lcd_->setTextSize(1);
            break;
    }
}

void TouchPanel::drawKeyboardIcon(int cx, int cy, uint16_t color) {
    // ~28x16 mini keyboard centered on (cx, cy): rounded body, 2 rows of
    // 6 key-dots, plus a spacebar across the bottom.
    const int x = cx - 14;
    const int y = cy - 8;
    lcd_->drawRoundRect(x, y, 28, 16, 2, color);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 6; ++col) {
            lcd_->fillRect(x + 3 + col * 4, y + 3 + row * 4, 3, 2, color);
        }
    }
    lcd_->fillRect(x + 6, y + 11, 16, 2, color);  // spacebar
}

void TouchPanel::drawScrollArrow(int cx, int cy, uint16_t color) {
    // Right-pointing filled triangle, ~16w x 20h, centered on (cx, cy).
    lcd_->fillTriangle(cx - 8, cy - 10,
                       cx - 8, cy + 10,
                       cx + 8, cy,
                       color);
}

void TouchPanel::renderButton(int idx) {
    drawButton(idx, idx == pressedIdx_);
}

void TouchPanel::drawTitle() {
    if (!lcd_) return;
    // Clear title row (y=0..15, size-2 text is 16 px tall starting at kTitleY=4
    // — clear y=0..23 to wipe any prior longer title).
    lcd_->fillRect(kMidX, 0, kMidW, kStatY, kBarBg);
    if (!title_[0]) return;
    lcd_->setTextDatum(middle_center);
    lcd_->setTextColor(TFT_WHITE, kBarBg);
    lcd_->setTextSize(2);
    lcd_->drawString(title_, kMidX + kMidW / 2, kTitleY + 8);
    lcd_->setTextDatum(top_left);
    lcd_->setTextSize(1);
}

void TouchPanel::drawStatus() {
    if (!lcd_) return;
    // Clear two-line status area (y=24..39, plus 1 px padding on each side)
    lcd_->fillRect(kMidX, kStatY, kMidW, 16, kBarBg);
    lcd_->setTextSize(1);
    lcd_->setTextDatum(top_left);
    lcd_->setTextColor(TFT_WHITE, kBarBg);
    lcd_->setCursor(kMidX + 2, kStatY);
    lcd_->print(wifiLine_);
    lcd_->setCursor(kMidX + 2, kStatY + 8);
    lcd_->print(telLine_);
}

void TouchPanel::render() {
    if (!lcd_) return;
    lcd_->fillRect(0, 0, 320, STRIP_H, kBarBg);
    for (int i = 0; i < N_BUTTONS; ++i) renderButton(i);
    drawTitle();
    drawStatus();
    // 2-px white separator + 2-px black buffer between the strip and the
    // console area below (console Y_ORIGIN=48 unchanged).
    lcd_->fillRect(0, STRIP_H - 4, 320, 2, TFT_WHITE);
    lcd_->fillRect(0, STRIP_H - 2, 320, 2, TFT_BLACK);
}

void TouchPanel::setTitle(const char* title) {
    const char* t = title ? title : "";
    if (strncmp(t, title_, sizeof(title_)) == 0) return;
    strncpy(title_, t, sizeof(title_) - 1);
    title_[sizeof(title_) - 1] = 0;
    drawTitle();
}

void TouchPanel::setStatus(const char* wifiLine, const char* telLine) {
    const char* w = wifiLine  ? wifiLine  : "";
    const char* b = telLine ? telLine : "";
    if (strncmp(w, wifiLine_,  sizeof(wifiLine_))  == 0
     && strncmp(b, telLine_, sizeof(telLine_)) == 0) {
        return;
    }
    strncpy(wifiLine_,  w, sizeof(wifiLine_)  - 1); wifiLine_[sizeof(wifiLine_)   - 1] = 0;
    strncpy(telLine_, b, sizeof(telLine_) - 1); telLine_[sizeof(telLine_) - 1] = 0;
    drawStatus();
}

int TouchPanel::hitTest(int x, int y) const {
    if (y < 0 || y >= STRIP_H) return -1;
    if (x >= kSetupX  && x < kSetupX  + BTN_W) return 0;  // SETUP
    if (x >= kKbdX    && x < kKbdX    + BTN_W) return 1;  // KBD
    if (x >= kScrollX && x < kScrollX + BTN_W) return 2;  // SCROLL
    return -1;
}

TouchPanel::Action TouchPanel::poll() {
    if (!lcd_) return Action::NONE;
    int32_t tx = 0, ty = 0;
    bool touched = (lcd_->getTouch(&tx, &ty) != 0);

    Action act = Action::NONE;

    if (touched && !prevTouched_) {
        int idx = hitTest(tx, ty);
        if (idx >= 0) {
            pressedIdx_ = idx;
            pressedAt_  = millis();
            renderButton(idx);
            switch (idx) {
                case 0: act = Action::SETUP;  break;
                case 1: act = Action::KBD;    break;
                case 2: act = Action::SCROLL; break;
            }
        }
    }

    if (pressedIdx_ >= 0 && (millis() - pressedAt_) > 120) {
        int idx = pressedIdx_;
        pressedIdx_ = -1;
        renderButton(idx);
    }

    prevTouched_ = touched;
    return act;
}

// ---- Calibration persistence (unchanged) ----------------------------------
static constexpr const char* kCalPath = "/touch_cal.bin";

bool TouchPanel::loadCalibration() {
    if (!lcd_) return false;
    File f = SD.open(kCalPath, FILE_READ);
    if (!f) return false;
    uint16_t params[8];
    size_t n = f.read(reinterpret_cast<uint8_t*>(params), sizeof(params));
    f.close();
    if (n != sizeof(params)) return false;
    lcd_->setTouchCalibrate(params);
    Serial.println("[touch] calibration loaded");
    return true;
}

bool TouchPanel::calibrateAndSave() {
    if (!lcd_) return false;
    uint16_t params[8];
    lcd_->fillScreen(TFT_BLACK);
    lcd_->setTextColor(TFT_WHITE, TFT_BLACK);
    lcd_->setTextDatum(middle_center);
    lcd_->setTextSize(1);
    lcd_->drawString("Touch each corner target", 160, 20);
    lcd_->setTextDatum(top_left);
    lcd_->calibrateTouch(params, TFT_WHITE, TFT_BLACK, 20);

    File f = SD.open(kCalPath, FILE_WRITE);
    if (!f) { Serial.println("[touch] cal save FAIL"); return false; }
    f.write(reinterpret_cast<const uint8_t*>(params), sizeof(params));
    f.close();
    Serial.println("[touch] calibration saved");
    return true;
}
