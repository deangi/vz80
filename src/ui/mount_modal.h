#pragma once
#include <LovyanGFX.hpp>

// Full-screen overlay for mounting / unmounting disk images to drives A..D.
// The sketch drives it:
//   open(lcd);                       // begins modal, paints UI
//   while (poll() == CONTINUE) {}    // until user presses OK/CANCEL/UNMOUNT
//   apply result using drive() + filename()
//   close();
// While the modal is open, the Z80 task should be suspended so BDOS can't
// grab a half-switched disk.
class MountModal {
public:
    enum class Result : uint8_t { CONTINUE, OK, CANCEL, UNMOUNT };

    bool open(lgfx::LGFX_Device* lcd);
    void close();
    bool isOpen() const { return lcd_ != nullptr; }

    Result poll();

    uint8_t     drive()    const { return drive_; }            // 0..3 (A..D)
    const char* filename() const {
        return (sel_ >= 0 && sel_ < nFiles_) ? files_[sel_] : nullptr;
    }

    // All four drives mountable. .hdd files use auto-detected geometry
    // (26 sec x 128 byte, tracks derived from file size).
    static bool driveSupported(uint8_t d) { return d < 4; }

private:
    // Layout constants — k-prefixed to dodge #define collisions from the
    // Arduino ESP32 core (e.g. ROW_H / LIST_H are common macro names).
    static constexpr int kTitleY   = 0;
    static constexpr int kTitleH   = 16;
    static constexpr int kTabsY    = 16;
    static constexpr int kTabsH    = 32;
    static constexpr int kScrollY  = 48;
    static constexpr int kScrollH  = 32;
    static constexpr int kListY    = 80;
    static constexpr int kRowH     = 30;
    static constexpr int kRowsVis  = 4;
    static constexpr int kListH    = kRowH * kRowsVis;          // 120
    static constexpr int kActionY  = 200;
    static constexpr int kActionH  = 40;

    static constexpr int kMaxFiles = 32;
    static constexpr int kMaxName  = 48;

    lgfx::LGFX_Device* lcd_ = nullptr;
    uint8_t drive_  = 0;
    int     sel_    = -1;       // index into files_
    int     scroll_ = 0;
    bool    prevTouched_ = true;  // ignore currently-held touch on open

    char     files_[kMaxFiles][kMaxName]{};
    uint32_t sizes_[kMaxFiles]{};
    int      nFiles_ = 0;

    void scanFiles();
    void render();
    void renderTitle();
    void renderDriveTabs();
    void renderScrollBar();
    void renderList();
    void renderActions();

    // Create a new empty 256,256-byte .dsk file on SD with a unique name
    // (newdisk_NNN.dsk, NNN auto-incremented). Returns true on success.
    bool createNewDisk();

    Result hitTest(int x, int y);
};
