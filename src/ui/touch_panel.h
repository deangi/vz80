#pragma once
#include <LovyanGFX.hpp>

// Top strip layout:
//   [SETUP] [KBD]   <title>       [SCROLL]
//                   wifi: ...
//                   tel: ...
//
// Strip height: STRIP_H (48 px). Three 48x48 buttons (SETUP + KBD on the
// left, SCROLL on the right); the middle 176 px holds a centered title
// in the upper portion and a 16 px tall, 22-column status area in the
// lower portion (two lines of 6x8 size-1 text). Console area below the
// strip is unchanged (rows 48..239, 24 lines of 8 px).
// (BT was removed — see vZ80.ino top-of-file note on the internal-RAM
// constraint that ruled out BT on this board.)

class TouchPanel {
public:
    enum class Action : uint8_t {
        NONE,
        SETUP,    // open setup popup (REBOOT/MOUNT/CLEAR)
        KBD,      // toggle on-screen keyboard modal (M8)
        SCROLL,   // cycle console view position
    };

    static constexpr int STRIP_H    = 48;
    static constexpr int BTN_W      = 48;     // all three buttons are square
    static constexpr int N_BUTTONS  = 3;

    bool begin(lgfx::LGFX_Device* lcd);

    // Title from config.json — call before render() to set, or any time
    // (will redraw the title region).
    void setTitle(const char* title);

    // Per-second status update from the main loop. Each string ≤ 30 chars.
    // Pass nullptr or "" to clear that line. Skips redraw when unchanged.
    void setStatus(const char* wifiLine, const char* telLine);

    // Repaint everything in the strip (call after modals close or on a
    // forced redraw).
    void render();
    void renderButton(int idx);

    // Poll touch; returns the Action that just went down, or NONE.
    Action poll();

    // Scroll-position index (0..3) mapped to console view cols 0/9/18/27.
    uint8_t  scrollIndex() const { return scrollIdx_; }
    void     advanceScroll()     { scrollIdx_ = (scrollIdx_ + 1) % 4; }
    static uint16_t scrollColFor(uint8_t idx);

    // Touch calibration (unchanged).
    bool calibrateAndSave();
    bool loadCalibration();

private:
    lgfx::LGFX_Device* lcd_ = nullptr;
    bool    prevTouched_= false;
    int8_t  pressedIdx_ = -1;
    uint32_t pressedAt_ = 0;
    uint8_t  scrollIdx_ = 0;

    char    title_[32]    = "";
    char    wifiLine_[40] = "";
    char    telLine_[40]= "";

    int  hitTest(int x, int y) const;
    void drawButton(int idx, bool pressed);
    void drawTitle();
    void drawStatus();
    void drawKeyboardIcon(int cx, int cy, uint16_t color);
    void drawScrollArrow(int cx, int cy, uint16_t color);

    // Geometry helpers (constexpr-style for clarity).
    static constexpr int kSetupX  = 0;
    static constexpr int kKbdX    = BTN_W;        // 48
    static constexpr int kScrollX = 320 - BTN_W;  // 272
    static constexpr int kMidX    = 2 * BTN_W;    // 96
    static constexpr int kMidW    = 320 - 3 * BTN_W;  // 176
    static constexpr int kTitleY  = 4;            // size-2 title sits y=4..19
    static constexpr int kStatY   = 24;           // 2 lines @ 8 px each = y=24..39

    // Visual style — forest green bar, beveled buttons, white separator.
    static constexpr uint16_t kBarBg   = 0x0320;  // forest green (RGB565)
    static constexpr uint16_t kBtnFace = kBarBg;  // match bar background
    static constexpr uint16_t kBtnHi   = 0xC618;  // light grey (top/left bevel)
    static constexpr uint16_t kBtnSh   = 0x1082;  // dark grey  (bottom/right bevel)
};
