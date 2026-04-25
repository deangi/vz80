#pragma once
#include <LovyanGFX.hpp>

// Full-screen popup brought up by the SETUP button on the top strip.
// Four buttons in a 2x2 grid:
//   [REBOOT]  [MOUNT]
//   [CLEAR ]  [BT   ]
// User taps one to pick an action, or swipes anywhere outside to cancel
// (BACK button at the bottom).
class SetupModal {
public:
    enum class Result : uint8_t {
        CONTINUE,    // still waiting for user
        REBOOT,      // cold-boot CP/M
        MOUNT,       // open mount modal
        CLEAR,       // clear console
        BT,          // start BT pairing
        CANCEL,      // back without action
    };

    bool open(lgfx::LGFX_Device* lcd);
    void close();
    bool isOpen() const { return lcd_ != nullptr; }

    // Optional build identification shown beneath the buttons.
    void setVersion(const char* version, const char* date);

    Result poll();

private:
    static constexpr int kTitleY  = 8;
    static constexpr int kTitleH  = 24;
    static constexpr int kBtnAreaY = 40;
    static constexpr int kBtnW    = 140;
    static constexpr int kBtnH    = 60;
    static constexpr int kBtnPadX = 20;
    static constexpr int kBtnPadY = 20;
    static constexpr int kBackY   = 200;
    static constexpr int kBackH   = 32;

    lgfx::LGFX_Device* lcd_ = nullptr;
    bool prevTouched_ = true;     // ignore already-held touch on open

    char version_[16] = "";
    char date_[16]    = "";

    void render();
    void drawButton(int x, int y, int w, int h, const char* label,
                    uint16_t bg, bool pressed);
    Result hitTest(int x, int y) const;
};
