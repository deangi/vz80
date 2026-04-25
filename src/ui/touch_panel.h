#pragma once
#include <LovyanGFX.hpp>

// 6-button control panel occupying the top 48 px of the 320x240 display.
// poll() should be called from the main loop on core 1; it reads the XPT2046
// touch through LovyanGFX and returns an Action on a press edge (button down).

class TouchPanel {
public:
    enum class Action : uint8_t {
        NONE,
        RUN_TOGGLE,   // STOP/RUN
        BOOT,         // cold boot A:
        CLEAR,        // local screen clear
        BT,           // (stub) Bluetooth status
        MOUNT,        // open mount modal
        SCROLL        // cycle console view position
    };

    static constexpr int STRIP_H    = 48;     // whole top strip: buttons + status
    static constexpr int BUTTON_H   = 40;     // buttons render only the top 40 px
    static constexpr int STATUS_Y   = 40;     // y of status footer
    static constexpr int STATUS_H   = 8;      // 1 line of size-1 text fits here
    static constexpr int BTN_W      = 53;     // 6 * 53 = 318 (+2 into last)
    static constexpr int N_BUTTONS  = 6;

    bool begin(lgfx::LGFX_Device* lcd);

    // Running state controls the RUN/STOP button label + color.
    void setRunning(bool running);
    bool running() const { return running_; }

    // Repaint all buttons (call after modals close or after full redraw).
    void render();
    void renderButton(int idx);

    // Status footer (8 px row beneath the buttons). Pass nullptr or "" to
    // clear. Skips redraw if the text hasn't changed since last call.
    void setStatus(const char* text, uint16_t fg = 0xFFFF, uint16_t bg = 0x0000);

    // Poll touch; returns the Action that just went down, or NONE.
    Action poll();

    // Scroll-position index (0..3) mapped to console view cols 0/9/18/27.
    uint8_t  scrollIndex() const { return scrollIdx_; }
    void     advanceScroll()     { scrollIdx_ = (scrollIdx_ + 1) % 4; }
    static uint16_t scrollColFor(uint8_t idx);

    // If touch feels miscalibrated, call this to run LovyanGFX's corner-tap
    // calibration and persist params to /touch_cal.bin. Returns true on save.
    bool calibrateAndSave();
    bool loadCalibration();

private:
    lgfx::LGFX_Device* lcd_ = nullptr;
    bool    running_    = true;
    bool    prevTouched_= false;
    int8_t  pressedIdx_ = -1;
    uint32_t pressedAt_ = 0;
    uint8_t  scrollIdx_ = 0;

    int hitTest(int x, int y) const;
    void drawButton(int idx, bool pressed);

    char     lastStatus_[64] = {0};
    uint16_t lastStatusFg_   = 0;
    uint16_t lastStatusBg_   = 0;
};
