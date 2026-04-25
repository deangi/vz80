#include "mount_modal.h"
#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#include <string.h>

static bool hasExt(const char* name, const char* ext) {
    size_t ln = strlen(name), le = strlen(ext);
    if (ln < le) return false;
    return strcasecmp(name + ln - le, ext) == 0;
}

bool MountModal::open(lgfx::LGFX_Device* lcd) {
    lcd_ = lcd;
    sel_ = -1;
    scroll_ = 0;
    drive_ = 0;
    prevTouched_ = true;   // ignore touch already held on open
    scanFiles();
    render();
    return true;
}

void MountModal::close() {
    lcd_ = nullptr;
}

void MountModal::scanFiles() {
    nFiles_ = 0;
    File root = SD.open("/");
    if (!root) return;
    File f;
    while ((f = root.openNextFile())) {
        if (!f.isDirectory()) {
            const char* nm = f.name();
            if (nm[0] == '/') ++nm;
            if ((hasExt(nm, ".dsk") || hasExt(nm, ".hdd")) && nFiles_ < kMaxFiles) {
                strncpy(files_[nFiles_], nm, kMaxName - 1);
                files_[nFiles_][kMaxName - 1] = '\0';
                sizes_[nFiles_] = f.size();
                ++nFiles_;
            }
        }
        f.close();
    }
    root.close();
}

// -------- rendering --------------------------------------------------------

void MountModal::render() {
    if (!lcd_) return;
    lcd_->fillScreen(TFT_BLACK);
    renderTitle();
    renderDriveTabs();
    renderScrollBar();
    renderList();
    renderActions();
}

void MountModal::renderTitle() {
    lcd_->fillRect(0, kTitleY, 320, kTitleH, TFT_NAVY);
    lcd_->setTextColor(TFT_WHITE, TFT_NAVY);
    lcd_->setTextDatum(middle_center);
    lcd_->setTextSize(1);
    lcd_->drawString("MOUNT DRIVE - tap drive, file, then OK",
                     160, kTitleY + kTitleH / 2);
}

void MountModal::renderDriveTabs() {
    const char* labels[4] = { "A:", "B:", "C:", "D:" };
    int bw = 80;
    for (int i = 0; i < 4; ++i) {
        int x = i * bw;
        bool sel = (i == drive_);
        bool sup = driveSupported(i);
        uint16_t bg = sel ? TFT_DARKGREEN : (sup ? 0x2104 : 0x18C3);
        uint16_t fg = sup ? TFT_WHITE : TFT_DARKGREY;
        lcd_->fillRect(x + 1, kTabsY + 1, bw - 2, kTabsH - 2, bg);
        lcd_->drawRect(x, kTabsY, bw, kTabsH, TFT_DARKGREY);
        lcd_->setTextColor(fg, bg);
        lcd_->setTextDatum(middle_center);
        lcd_->setTextSize(2);
        lcd_->drawString(labels[i], x + bw / 2, kTabsY + kTabsH / 2);
    }
    lcd_->setTextSize(1);
}

void MountModal::renderScrollBar() {
    lcd_->fillRect(0, kScrollY, 320, kScrollH, TFT_BLACK);

    char lbl[40];
    snprintf(lbl, sizeof(lbl), "Files: %d", nFiles_);
    lcd_->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    lcd_->setTextDatum(middle_left);
    lcd_->setTextSize(1);
    lcd_->drawString(lbl, 6, kScrollY + kScrollH / 2);

    // UP / DOWN arrows on right (40 wide each)
    int upX = 240, dnX = 280;
    lcd_->fillRect(upX + 1, kScrollY + 1, 38, kScrollH - 2, 0x2104);
    lcd_->drawRect(upX, kScrollY, 40, kScrollH, TFT_DARKGREY);
    lcd_->fillRect(dnX + 1, kScrollY + 1, 38, kScrollH - 2, 0x2104);
    lcd_->drawRect(dnX, kScrollY, 40, kScrollH, TFT_DARKGREY);
    lcd_->setTextDatum(middle_center);
    lcd_->setTextColor(TFT_WHITE, 0x2104);
    lcd_->setTextSize(2);
    lcd_->drawString("^", upX + 20, kScrollY + kScrollH / 2);
    lcd_->drawString("v", dnX + 20, kScrollY + kScrollH / 2);
    lcd_->setTextSize(1);
}

void MountModal::renderList() {
    lcd_->fillRect(0, kListY, 320, kListH, TFT_BLACK);
    for (int r = 0; r < kRowsVis; ++r) {
        int idx = scroll_ + r;
        int y = kListY + r * kRowH;
        if (idx >= nFiles_) {
            lcd_->drawRect(0, y, 320, kRowH, 0x2104);
            continue;
        }
        bool selRow = (idx == sel_);
        uint16_t bg = selRow ? TFT_DARKGREEN : 0x1082;
        uint16_t fg = TFT_WHITE;

        lcd_->fillRect(1, y + 1, 318, kRowH - 2, bg);
        lcd_->drawRect(0, y, 320, kRowH, TFT_DARKGREY);

        lcd_->setTextColor(fg, bg);
        lcd_->setTextDatum(middle_left);
        lcd_->setTextSize(1);
        lcd_->drawString(files_[idx], 8, y + kRowH / 2);

        char sz[20];
        uint32_t s = sizes_[idx];
        if (s >= 1024 * 1024)
            snprintf(sz, sizeof(sz), "%u MB", (unsigned)(s / (1024 * 1024)));
        else
            snprintf(sz, sizeof(sz), "%u KB", (unsigned)(s / 1024));
        lcd_->setTextDatum(middle_right);
        lcd_->drawString(sz, 312, y + kRowH / 2);
    }
    lcd_->setTextDatum(top_left);
}

void MountModal::renderActions() {
    struct { const char* label; uint16_t bg; int x, w; } btns[4] = {
        { "CANCEL", TFT_MAROON,     0,  80 },
        { "UNMNT",  TFT_DARKGREY,  80,  80 },
        { "NEW",    TFT_NAVY,     160,  80 },
        { "OK",     TFT_DARKGREEN,240,  80 }
    };
    for (int i = 0; i < 4; ++i) {
        lcd_->fillRect(btns[i].x + 1, kActionY + 1,
                       btns[i].w - 2, kActionH - 2, btns[i].bg);
        lcd_->drawRect(btns[i].x, kActionY, btns[i].w, kActionH, TFT_DARKGREY);
        lcd_->setTextColor(TFT_WHITE, btns[i].bg);
        lcd_->setTextDatum(middle_center);
        lcd_->setTextSize(2);
        lcd_->drawString(btns[i].label,
                         btns[i].x + btns[i].w / 2,
                         kActionY + kActionH / 2);
    }
    lcd_->setTextSize(1);
}

bool MountModal::createNewDisk() {
    // Find first unused name newdisk_NNN.dsk (001..999).
    char name[24];
    char path[32];
    for (int n = 1; n < 1000; ++n) {
        snprintf(name, sizeof(name), "newdisk_%03d.dsk", n);
        snprintf(path, sizeof(path), "/%s", name);
        if (!SD.exists(path)) break;
        if (n == 999) {
            Serial.println("[mount] no free newdisk_NNN slot");
            return false;
        }
    }

    File f = SD.open(path, FILE_WRITE);
    if (!f) { Serial.printf("[mount] create %s FAIL\n", path); return false; }

    // 256,256 bytes = 250 * 1024 + 256. Fill with 0xE5 — CP/M's "blank
    // sector" pattern (deleted/empty directory entries). A zero-filled
    // image would look to CCP like 32 garbage entries; 0xE5 makes the
    // disk immediately usable without running FORMAT first.
    static const size_t kTotal = 256u * 1024u + 256u;  // == 256256
    uint8_t blank[1024];
    memset(blank, 0xE5, sizeof(blank));
    size_t written = 0;
    while (written < kTotal) {
        size_t chunk = (kTotal - written) >= sizeof(blank) ? sizeof(blank)
                                                           : (kTotal - written);
        if (f.write(blank, chunk) != chunk) {
            Serial.printf("[mount] write %s short at %u\n", path, (unsigned)written);
            f.close();
            return false;
        }
        written += chunk;
    }
    f.close();
    Serial.printf("[mount] created %s (%u bytes)\n", path, (unsigned)kTotal);
    return true;
}

// -------- hit test ---------------------------------------------------------

MountModal::Result MountModal::hitTest(int x, int y) {
    // Drive tabs
    if (y >= kTabsY && y < kTabsY + kTabsH) {
        int d = x / 80;
        if (d >= 0 && d < 4 && driveSupported(d)) {
            drive_ = d;
            renderDriveTabs();
        }
        return Result::CONTINUE;
    }
    // Scroll arrows
    if (y >= kScrollY && y < kScrollY + kScrollH) {
        int maxScroll = nFiles_ - kRowsVis;
        if (maxScroll < 0) maxScroll = 0;
        if (x >= 240 && x < 280) {
            if (scroll_ > 0) { --scroll_; renderList(); renderScrollBar(); }
        } else if (x >= 280 && x < 320) {
            if (scroll_ < maxScroll) { ++scroll_; renderList(); renderScrollBar(); }
        }
        return Result::CONTINUE;
    }
    // File rows
    if (y >= kListY && y < kListY + kListH) {
        int r = (y - kListY) / kRowH;
        int idx = scroll_ + r;
        if (idx >= 0 && idx < nFiles_) {
            sel_ = idx;
            renderList();
        }
        return Result::CONTINUE;
    }
    // Action buttons (4 x 80px)
    if (y >= kActionY && y < kActionY + kActionH) {
        if (x < 80)        return Result::CANCEL;
        if (x < 160)       return Result::UNMOUNT;
        if (x < 240) {
            // NEW handled internally — create file, refresh list, stay open.
            createNewDisk();
            scanFiles();
            renderScrollBar();
            renderList();
            return Result::CONTINUE;
        }
        return Result::OK;
    }
    return Result::CONTINUE;
}

// -------- poll -------------------------------------------------------------

MountModal::Result MountModal::poll() {
    if (!lcd_) return Result::CANCEL;
    int32_t tx = 0, ty = 0;
    bool touched = (lcd_->getTouch(&tx, &ty) != 0);

    Result r = Result::CONTINUE;
    if (touched && !prevTouched_) {
        r = hitTest(tx, ty);
    }
    prevTouched_ = touched;
    return r;
}
