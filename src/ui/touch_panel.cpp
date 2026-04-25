#include "touch_panel.h"
#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#include <string.h>

static const char* kLabels[TouchPanel::N_BUTTONS] = { "SET", "SCR" };

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
    int x = (idx == 0) ? kSetupX : kScrollX;
    int w = BTN_W;

    uint16_t bg = pressed ? TFT_ORANGE : 0x2104;  // dark slate
    uint16_t fg = TFT_WHITE;
    const char* label = kLabels[idx];

    lcd_->fillRect(x + 1, 1, w - 2, STRIP_H - 2, bg);
    lcd_->drawRect(x, 0, w, STRIP_H, TFT_DARKGREY);

    lcd_->setTextDatum(middle_center);
    lcd_->setTextColor(fg, bg);
    lcd_->setTextSize(2);
    lcd_->drawString(label, x + w / 2, STRIP_H / 2);
    lcd_->setTextDatum(top_left);
    lcd_->setTextSize(1);
}

void TouchPanel::renderButton(int idx) {
    drawButton(idx, idx == pressedIdx_);
}

void TouchPanel::drawTitle() {
    if (!lcd_) return;
    // Clear title row (y=0..15, size-2 text is 16 px tall starting at kTitleY=4
    // — clear y=0..23 to wipe any prior longer title).
    lcd_->fillRect(kMidX, 0, kMidW, kStatY, TFT_BLACK);
    if (!title_[0]) return;
    lcd_->setTextDatum(middle_center);
    lcd_->setTextColor(TFT_CYAN, TFT_BLACK);
    lcd_->setTextSize(2);
    lcd_->drawString(title_, kMidX + kMidW / 2, kTitleY + 8);
    lcd_->setTextDatum(top_left);
    lcd_->setTextSize(1);
}

void TouchPanel::drawStatus() {
    if (!lcd_) return;
    // Clear two-line status area (y=24..39, plus 1 px padding on each side)
    lcd_->fillRect(kMidX, kStatY, kMidW, 16, TFT_BLACK);
    lcd_->setTextSize(1);
    lcd_->setTextDatum(top_left);
    lcd_->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    lcd_->setCursor(kMidX + 2, kStatY);
    lcd_->print(wifiLine_);
    lcd_->setCursor(kMidX + 2, kStatY + 8);
    lcd_->print(btTelLine_);
}

void TouchPanel::render() {
    if (!lcd_) return;
    lcd_->fillRect(0, 0, 320, STRIP_H, TFT_BLACK);
    for (int i = 0; i < N_BUTTONS; ++i) renderButton(i);
    drawTitle();
    drawStatus();
}

void TouchPanel::setTitle(const char* title) {
    const char* t = title ? title : "";
    if (strncmp(t, title_, sizeof(title_)) == 0) return;
    strncpy(title_, t, sizeof(title_) - 1);
    title_[sizeof(title_) - 1] = 0;
    drawTitle();
}

void TouchPanel::setStatus(const char* wifiLine, const char* btTelLine) {
    const char* w = wifiLine  ? wifiLine  : "";
    const char* b = btTelLine ? btTelLine : "";
    if (strncmp(w, wifiLine_,  sizeof(wifiLine_))  == 0
     && strncmp(b, btTelLine_, sizeof(btTelLine_)) == 0) {
        return;
    }
    strncpy(wifiLine_,  w, sizeof(wifiLine_)  - 1); wifiLine_[sizeof(wifiLine_)   - 1] = 0;
    strncpy(btTelLine_, b, sizeof(btTelLine_) - 1); btTelLine_[sizeof(btTelLine_) - 1] = 0;
    drawStatus();
}

int TouchPanel::hitTest(int x, int y) const {
    if (y < 0 || y >= STRIP_H) return -1;
    if (x >= kSetupX  && x < kSetupX  + BTN_W) return 0;  // SETUP
    if (x >= kScrollX && x < kScrollX + BTN_W) return 1;  // SCROLL
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
                case 1: act = Action::SCROLL; break;
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
