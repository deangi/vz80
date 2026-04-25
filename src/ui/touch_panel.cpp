#include "touch_panel.h"
#include <Arduino.h>
#include <SD.h>
#include <FS.h>

static const char* kLabels[TouchPanel::N_BUTTONS] = {
    "RUN", "BOOT", "CLR", "BT", "MNT", "SCRL"
};

uint16_t TouchPanel::scrollColFor(uint8_t idx) {
    static const uint16_t kCols[4] = { 0, 9, 18, 27 };
    return kCols[idx & 3];
}

bool TouchPanel::begin(lgfx::LGFX_Device* lcd) {
    lcd_ = lcd;
    // Try to load previously saved calibration (no-op if missing).
    loadCalibration();
    render();
    return true;
}

void TouchPanel::setRunning(bool running) {
    if (running == running_) return;
    running_ = running;
    renderButton(0);
}

void TouchPanel::drawButton(int idx, bool pressed) {
    if (!lcd_ || idx < 0 || idx >= N_BUTTONS) return;
    int x = idx * BTN_W;
    int w = (idx == N_BUTTONS - 1) ? (320 - x) : BTN_W;

    uint16_t bg = 0x2104;      // dark slate
    uint16_t fg = TFT_WHITE;
    const char* label = kLabels[idx];

    if (idx == 0) {
        bg = running_ ? TFT_DARKGREEN : TFT_MAROON;
        label = running_ ? "RUN" : "STOP";
    }
    if (pressed) bg = TFT_ORANGE;

    lcd_->fillRect(x + 1, 1, w - 2, BUTTON_H - 2, bg);
    lcd_->drawRect(x, 0, w, BUTTON_H, TFT_DARKGREY);

    lcd_->setTextDatum(middle_center);
    lcd_->setTextColor(fg, bg);
    lcd_->setTextSize(2);
    lcd_->drawString(label, x + w / 2, BUTTON_H / 2);
    lcd_->setTextDatum(top_left);
    lcd_->setTextSize(1);
}

void TouchPanel::renderButton(int idx) {
    drawButton(idx, idx == pressedIdx_);
}

void TouchPanel::render() {
    if (!lcd_) return;
    lcd_->fillRect(0, 0, 320, STRIP_H, TFT_BLACK);
    for (int i = 0; i < N_BUTTONS; ++i) renderButton(i);
    // Re-draw cached status (if any) into the freshly cleared footer.
    if (lastStatus_[0]) {
        lcd_->fillRect(0, STATUS_Y, 320, STATUS_H, lastStatusBg_);
        lcd_->setTextColor(lastStatusFg_, lastStatusBg_);
        lcd_->setTextSize(1);
        lcd_->setCursor(2, STATUS_Y);
        lcd_->print(lastStatus_);
    }
}

void TouchPanel::setStatus(const char* text, uint16_t fg, uint16_t bg) {
    if (!lcd_) return;
    const char* t = text ? text : "";
    if (strncmp(t, lastStatus_, sizeof(lastStatus_)) == 0
        && fg == lastStatusFg_ && bg == lastStatusBg_) {
        return;  // no change
    }
    strncpy(lastStatus_, t, sizeof(lastStatus_) - 1);
    lastStatus_[sizeof(lastStatus_) - 1] = 0;
    lastStatusFg_ = fg;
    lastStatusBg_ = bg;

    lcd_->fillRect(0, STATUS_Y, 320, STATUS_H, bg);
    lcd_->setTextColor(fg, bg);
    lcd_->setTextSize(1);
    lcd_->setCursor(2, STATUS_Y);
    lcd_->print(lastStatus_);
}

int TouchPanel::hitTest(int x, int y) const {
    if (y < 0 || y >= BUTTON_H) return -1;  // status footer is non-touch
    if (x < 0 || x >= 320) return -1;
    int idx = x / BTN_W;
    if (idx >= N_BUTTONS) idx = N_BUTTONS - 1;
    return idx;
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
                case 0: act = Action::RUN_TOGGLE; break;
                case 1: act = Action::BOOT;       break;
                case 2: act = Action::CLEAR;      break;
                case 3: act = Action::BT;         break;
                case 4: act = Action::MOUNT;      break;
                case 5: act = Action::SCROLL;     break;
            }
        }
    }

    // Clear pressed-highlight after 120 ms.
    if (pressedIdx_ >= 0 && (millis() - pressedAt_) > 120) {
        int idx = pressedIdx_;
        pressedIdx_ = -1;
        renderButton(idx);
    }

    prevTouched_ = touched;
    return act;
}

// ---- Calibration persistence ----------------------------------------------
// LovyanGFX stores calibration as 8 uint16_t values.
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
