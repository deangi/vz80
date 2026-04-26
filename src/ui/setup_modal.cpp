#include "setup_modal.h"
#include <Arduino.h>
#include <string.h>

bool SetupModal::open(lgfx::LGFX_Device* lcd) {
    lcd_ = lcd;
    prevTouched_ = true;  // assume the user is still holding from the SETUP tap
    render();
    return true;
}

void SetupModal::close() { lcd_ = nullptr; }

void SetupModal::setVersion(const char* version, const char* date) {
    strncpy(version_, version ? version : "", sizeof(version_) - 1);
    version_[sizeof(version_) - 1] = 0;
    strncpy(date_,    date    ? date    : "", sizeof(date_) - 1);
    date_[sizeof(date_) - 1] = 0;
}

void SetupModal::drawButton(int x, int y, int w, int h, const char* label,
                            uint16_t bg, bool pressed) {
    uint16_t fillBg = pressed ? TFT_ORANGE : bg;
    lcd_->fillRect(x, y, w, h, fillBg);
    lcd_->drawRect(x, y, w, h, TFT_WHITE);
    lcd_->setTextDatum(middle_center);
    lcd_->setTextColor(TFT_WHITE, fillBg);
    lcd_->setTextSize(2);
    lcd_->drawString(label, x + w / 2, y + h / 2);
    lcd_->setTextDatum(top_left);
    lcd_->setTextSize(1);
}

void SetupModal::render() {
    if (!lcd_) return;
    lcd_->fillScreen(TFT_BLACK);

    // Title
    lcd_->setTextDatum(middle_center);
    lcd_->setTextColor(TFT_CYAN, TFT_BLACK);
    lcd_->setTextSize(2);
    lcd_->drawString("Setup", 160, kTitleY + kTitleH / 2);
    lcd_->setTextDatum(top_left);
    lcd_->setTextSize(1);

    // Top row: REBOOT + MOUNT. Bottom row: CLEAR centered.
    int gridW = kBtnW * 2 + kBtnPadX;
    int gx = (320 - gridW) / 2;
    int x0 = gx;
    int x1 = gx + kBtnW + kBtnPadX;
    int y0 = kBtnAreaY;
    int y1 = kBtnAreaY + kBtnH + kBtnPadY;
    int xClear = (320 - kBtnW) / 2;

    drawButton(x0,     y0, kBtnW, kBtnH, "REBOOT", TFT_DARKGREEN, false);
    drawButton(x1,     y0, kBtnW, kBtnH, "MOUNT",  TFT_NAVY,      false);
    drawButton(xClear, y1, kBtnW, kBtnH, "CLEAR",  TFT_MAROON,    false);

    // Version line above BACK button (size-1 text, light grey).
    if (version_[0] || date_[0]) {
        char line[40];
        snprintf(line, sizeof(line), "v%s   %s", version_, date_);
        lcd_->setTextDatum(middle_center);
        lcd_->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        lcd_->setTextSize(1);
        lcd_->drawString(line, 160, kBackY - 10);
        lcd_->setTextDatum(top_left);
    }

    // BACK strip at bottom
    drawButton(60, kBackY, 200, kBackH, "BACK", 0x2104, false);
}

SetupModal::Result SetupModal::hitTest(int x, int y) const {
    int gridW = kBtnW * 2 + kBtnPadX;
    int gx = (320 - gridW) / 2;
    int x0 = gx;
    int x1 = gx + kBtnW + kBtnPadX;
    int y0 = kBtnAreaY;
    int y1 = kBtnAreaY + kBtnH + kBtnPadY;
    int xClear = (320 - kBtnW) / 2;

    auto inRect = [&](int rx, int ry, int rw, int rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    };

    if (inRect(x0,     y0, kBtnW, kBtnH)) return Result::REBOOT;
    if (inRect(x1,     y0, kBtnW, kBtnH)) return Result::MOUNT;
    if (inRect(xClear, y1, kBtnW, kBtnH)) return Result::CLEAR;
    if (inRect(60, kBackY, 200, kBackH))  return Result::CANCEL;
    return Result::CONTINUE;
}

SetupModal::Result SetupModal::poll() {
    if (!lcd_) return Result::CONTINUE;
    int32_t tx = 0, ty = 0;
    bool touched = (lcd_->getTouch(&tx, &ty) != 0);
    Result r = Result::CONTINUE;
    if (touched && !prevTouched_) r = hitTest(tx, ty);
    prevTouched_ = touched;
    return r;
}
