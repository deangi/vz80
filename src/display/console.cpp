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
    uint8_t b = (uint8_t)c;
    // ESC always starts a fresh sequence, even if one was in progress.
    if (b == 0x1B) {
        pstate_ = PS_ESC;
        return;
    }
    // CAN (0x18) always cancels any in-progress escape sequence.
    if (b == 0x18) {
        pstate_    = PS_GROUND;
        csiIgnore_ = false;
        return;
    }
    // SUB (0x1A): VT-100 cancels in-progress escape; ADM-3A clears+homes.
    // Either way, parser state returns to ground.
    if (b == 0x1A) {
        if (mode_ == TM_ADM3A) clear();
        pstate_    = PS_GROUND;
        csiIgnore_ = false;
        return;
    }
    // ESC = takes two raw bytes as row/col — they must reach the parser
    // unfiltered even when their value is < 0x20 (top-of-screen rows/cols).
    if (pstate_ == PS_ADM_CUP_ROW) { parseAdmCupRow(b); return; }
    if (pstate_ == PS_ADM_CUP_COL) { parseAdmCupCol(b); return; }
    // Other C0 controls (CR, LF, BS, TAB, FF) execute immediately even
    // mid-escape, without disturbing parser state. MBASIC's WIDTH auto-wrap
    // can inject CR/LF inside an escape sequence — without this, the rest of
    // the sequence would be aborted and printed literally.
    if (b < 0x20 && pstate_ != PS_GROUND) {
        putcGround(b);
        return;
    }
    switch (pstate_) {
        case PS_GROUND:      putcGround(b);     return;
        case PS_ESC:         parseEsc(b);       return;
        case PS_CSI:         parseCsi(b);       return;
        case PS_ADM_CUP_ROW: parseAdmCupRow(b); return;
        case PS_ADM_CUP_COL: parseAdmCupCol(b); return;
    }
}

void Console::putcGround(uint8_t c) {
    switch (c) {
        case 0x08: // BS — cursor left (both modes)
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
        case 0x0B: // VT — ADM-3A cursor up; VT-100 ignores
            if (mode_ == TM_ADM3A && cy_ > 0) --cy_;
            return;
        case 0x0C: // FF — VT-100 clear+home; ADM-3A cursor right
            if (mode_ == TM_ADM3A) {
                if (cx_ < COLS - 1) ++cx_;
            } else {
                clear();
            }
            return;
        case 0x0D: // CR
            cx_ = 0;
            return;
        case 0x1E: // RS — home cursor (no clear). Useful for ADM-3A software
                   // and harmless in VT-100 mode.
            cx_ = 0;
            cy_ = 0;
            return;
        default:
            break;
    }
    if (c < 0x20) return;  // ignore other control codes
    writeCell(cy_, cx_, (char)c);
    ++cx_;
    if (cx_ >= COLS) newline();
}

void Console::parseEsc(uint8_t c) {
    switch (c) {
        case '[':  // CSI — VT-100; CSI handler stays active in both modes
            pstate_ = PS_CSI;
            paramCount_  = 0;
            csiParamIdx_ = 0;
            csiIgnore_   = false;
            params_[0] = params_[1] = 0;
            paramSeen_[0] = paramSeen_[1] = false;
            return;
        case 'E':  // VT-100 NEL (newline); ADM-3A would mean "insert line"
                   // which we don't implement — keep NEL in both modes.
            newline();
            pstate_ = PS_GROUND;
            return;
        case '7':  // DECSC — save cursor
            savedCx_ = cx_;
            savedCy_ = cy_;
            pstate_ = PS_GROUND;
            return;
        case '8':  // DECRC — restore cursor
            cx_ = savedCx_;
            cy_ = savedCy_;
            if (cx_ < 0) cx_ = 0; else if (cx_ >= COLS) cx_ = COLS - 1;
            if (cy_ < 0) cy_ = 0; else if (cy_ >= ROWS) cy_ = ROWS - 1;
            pstate_ = PS_GROUND;
            return;
        case '=':  // ADM-3A cursor address: next two bytes = row+0x20, col+0x20
            pstate_ = PS_ADM_CUP_ROW;
            return;
        case 'T':  // ADM-3A erase to end of line
            eraseInLine(0);
            pstate_ = PS_GROUND;
            return;
        case 'Y':  // ADM-3A erase to end of screen
            eraseInDisplay(0);
            pstate_ = PS_GROUND;
            return;
        case '*':  // ADM-3A clear screen + home (Kaypro/Lear-Siegler dialect)
            clear();
            pstate_ = PS_GROUND;
            return;
        default:   // unknown — silently drop
            pstate_ = PS_GROUND;
            return;
    }
}

void Console::parseAdmCupRow(uint8_t c) {
    admCupRow_ = (int)c - 0x20;
    pstate_ = PS_ADM_CUP_COL;
}

void Console::parseAdmCupCol(uint8_t c) {
    int col = (int)c - 0x20;
    cursorTo(admCupRow_, col);   // cursorTo clamps to valid range
    pstate_ = PS_GROUND;
}

void Console::parseCsi(uint8_t c) {
    if (csiIgnore_) {
        if (c >= '@' && c <= '~') {     // final byte ends the ignored seq
            csiIgnore_ = false;
            pstate_    = PS_GROUND;
        }
        return;
    }
    if (c >= '0' && c <= '9') {
        if (csiParamIdx_ < MAX_PARAMS) {
            params_[csiParamIdx_] = params_[csiParamIdx_] * 10 + (c - '0');
            paramSeen_[csiParamIdx_] = true;
            if (paramCount_ < csiParamIdx_ + 1) paramCount_ = csiParamIdx_ + 1;
        }
        return;
    }
    if (c == ';') {
        if (csiParamIdx_ < MAX_PARAMS - 1) ++csiParamIdx_;
        if (paramCount_ < csiParamIdx_ + 1) paramCount_ = csiParamIdx_ + 1;
        return;
    }
    if (c >= '@' && c <= '~') {         // final byte
        dispatchCsi(c);
        pstate_ = PS_GROUND;
        return;
    }
    // Private/intermediate byte we don't handle (e.g., '?' for DEC modes) —
    // swallow the rest of the sequence so it doesn't spill onto the screen.
    csiIgnore_ = true;
}

int Console::csiParam(int i, int defaultVal) const {
    if (i >= paramCount_) return defaultVal;
    return paramSeen_[i] ? params_[i] : defaultVal;
}

void Console::dispatchCsi(uint8_t finalByte) {
    switch (finalByte) {
        case 'A': cursorTo(cy_ - csiParam(0, 1), cx_); return;  // CUU
        case 'B': cursorTo(cy_ + csiParam(0, 1), cx_); return;  // CUD
        case 'C': cursorTo(cy_, cx_ + csiParam(0, 1)); return;  // CUF
        case 'D': cursorTo(cy_, cx_ - csiParam(0, 1)); return;  // CUB
        case 'H':                                                // CUP
        case 'f':                                                // HVP
            cursorTo(csiParam(0, 1) - 1, csiParam(1, 1) - 1);
            return;
        case 'J': eraseInDisplay(csiParam(0, 0)); return;       // ED
        case 'K': eraseInLine(csiParam(0, 0));    return;       // EL
        default: return;                                         // unknown — drop
    }
}

void Console::cursorTo(int row, int col) {
    if (row < 0) row = 0; else if (row >= ROWS) row = ROWS - 1;
    if (col < 0) col = 0; else if (col >= COLS) col = COLS - 1;
    cy_ = row;
    cx_ = col;
}

void Console::eraseInLine(int mode) {
    int from = 0, to = 0;
    if      (mode == 0) { from = cx_; to = COLS; }
    else if (mode == 1) { from = 0;   to = cx_ + 1; }
    else if (mode == 2) { from = 0;   to = COLS; }
    else return;
    if (to > COLS) to = COLS;
    for (int c = from; c < to; ++c) buf_[cy_][c] = ' ';
    dirty_[cy_] = true;
}

void Console::eraseInDisplay(int mode) {
    if (mode == 0) {                    // cursor → end of screen
        for (int c = cx_; c < COLS; ++c) buf_[cy_][c] = ' ';
        dirty_[cy_] = true;
        for (int r = cy_ + 1; r < ROWS; ++r) {
            memset(&buf_[r][0], ' ', COLS);
            dirty_[r] = true;
        }
    } else if (mode == 1) {             // start of screen → cursor
        for (int r = 0; r < cy_; ++r) {
            memset(&buf_[r][0], ' ', COLS);
            dirty_[r] = true;
        }
        int to = cx_ + 1; if (to > COLS) to = COLS;
        for (int c = 0; c < to; ++c) buf_[cy_][c] = ' ';
        dirty_[cy_] = true;
    } else if (mode == 2) {             // entire screen, cursor unchanged
        memset(buf_, ' ', sizeof(buf_));
        for (int r = 0; r < ROWS; ++r) dirty_[r] = true;
    }
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

void Console::setViewRowSpan(int topRow, int rowCount) {
    if (topRow < 0) topRow = 0;
    if (rowCount < 1) rowCount = 1;
    if (topRow + rowCount > ROWS) rowCount = ROWS - topRow;
    if (topRow == viewRowTop_ && rowCount == viewRowsCount_) return;
    viewRowTop_    = topRow;
    viewRowsCount_ = rowCount;
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
    // Skip rows outside the active vertical viewport (used when the
    // on-screen keyboard is open and only the last few rows are visible).
    if (row < viewRowTop_ || row >= viewRowTop_ + viewRowsCount_) return;
    lineSpr_.fillScreen(bg_);
    int first = viewCol_;
    int last  = viewCol_ + VIEW_COLS;
    if (last > COLS) last = COLS;
    for (int col = first; col < last; ++col) {
        char c = buf_[row][col];
        int sx = (col - first) * CELL_W;
        drawGlyphIntoSprite(sx, c);
    }
    int dispRow = row - viewRowTop_;
    lineSpr_.pushSprite(lcd_, 0, Y_ORIGIN + dispRow * CELL_H);
}

void Console::render() {
    if (!lcd_) return;
    for (int r = 0; r < ROWS; ++r) {
        if (!dirty_[r]) continue;
        renderLine(r);
        dirty_[r] = false;
    }
}
