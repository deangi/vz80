#include "console.h"
#include "font5x7.h"
#include <string.h>

bool Console::begin(lgfx::LGFX_Device* lcd, uint16_t fg, uint16_t bg) {
    lcd_ = lcd;
    fg_ = fg;
    bg_ = bg;

    // No setParent() in LGFX v1 - we always use pushSprite(lcd_, x, y) instead.
    lineSpr_.setPsram(false);
    lineSpr_.setColorDepth(16);
    if (!lineSpr_.createSprite(lcd_->width(), CELL_H)) {
        return false;
    }
    lineSpr_.setTextSize(1);

    clear();
    // Paint the entire console area once so we start from a known state.
    lcd_->fillRect(0, Y_ORIGIN, lcd_->width(), ROWS * CELL_H, bg_);
    return true;
}

void Console::clear() {
    memset(buf_, ' ', sizeof(buf_));
    for (int r = 0; r < ROWS; ++r) dirty_[r] = true;
    cx_ = 0;
    cy_ = 0;
}

void Console::markAllDirty() {
    for (int r = 0; r < ROWS; ++r) dirty_[r] = true;
}

void Console::newline() {
    cx_ = 0;
    if (cy_ < ROWS - 1) {
        ++cy_;
    } else {
        scrollUp();
    }
}

void Console::scrollUp() {
    memmove(&buf_[0][0], &buf_[1][0], (ROWS - 1) * COLS);
    memset(&buf_[ROWS - 1][0], ' ', COLS);
    for (int r = 0; r < ROWS; ++r) dirty_[r] = true;
}

void Console::writeCell(int row, int col, char c) {
    if (row < 0 || row >= ROWS || col < 0 || col >= COLS) return;
    buf_[row][col] = c;
    dirty_[row] = true;
}

void Console::putc(char c) {
    switch ((uint8_t)c) {
        case 0x08: // BS
            if (cx_ > 0) --cx_;
            return;
        case 0x09: // TAB
            do {
                writeCell(cy_, cx_, ' ');
                ++cx_;
                if (cx_ >= COLS) { newline(); break; }
            } while (cx_ % TAB_STOP != 0);
            return;
        case 0x0A: // LF
            newline();
            return;
        case 0x0C: // FF
            clear();
            return;
        case 0x0D: // CR
            cx_ = 0;
            return;
        default:
            break;
    }
    if ((uint8_t)c < 0x20) return;  // ignore other control codes for now
    writeCell(cy_, cx_, c);
    ++cx_;
    if (cx_ >= COLS) newline();
}

void Console::puts(const char* s) {
    while (*s) putc(*s++);
}

void Console::viewLeft(int cols) {
    setView(viewCol_ - cols);
}

void Console::viewRight(int cols) {
    setView(viewCol_ + cols);
}

void Console::setView(int col) {
    if (col < 0) col = 0;
    int maxView = COLS - VIEW_COLS;
    if (col > maxView) col = maxView;
    if (col == viewCol_) return;
    viewCol_ = col;
    markAllDirty();
}

void Console::drawGlyphIntoSprite(int sx, char c) {
    const uint8_t* g = font5x7_glyph(c);
    for (int col = 0; col < 5; ++col) {
        uint8_t bits = g[col];
        for (int row = 0; row < 7; ++row) {
            if (bits & (1u << row)) {
                lineSpr_.drawPixel(sx + col, row, fg_);
            }
        }
    }
    // 6th column and 8th row remain bg_ (cleared before this call).
}

void Console::renderLine(int row) {
    lineSpr_.fillScreen(bg_);
    int first = viewCol_;
    int last  = viewCol_ + VIEW_COLS;
    if (last > COLS) last = COLS;
    for (int col = first; col < last; ++col) {
        char c = buf_[row][col];
        int sx = (col - first) * CELL_W;
        drawGlyphIntoSprite(sx, c);
    }
    lineSpr_.pushSprite(lcd_, 0, Y_ORIGIN + row * CELL_H);
}

void Console::render() {
    if (!lcd_) return;
    for (int r = 0; r < ROWS; ++r) {
        if (!dirty_[r]) continue;
        renderLine(r);
        dirty_[r] = false;
    }
}
