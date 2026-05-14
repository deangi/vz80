#pragma once
#include <LovyanGFX.hpp>

// 80x24 logical console, 53 columns visible at once on the 320-wide panel.
// Rendered into display rows 48..239 (192 px tall, 24 rows * 8 px).
// Horizontal viewport can pan across columns 0..79.

class Console {
public:
    static constexpr int COLS       = 80;
    static constexpr int ROWS       = 24;
    static constexpr int VIEW_COLS  = 53;   // visible columns at a time
    static constexpr int CELL_W     = 6;    // 5 glyph + 1 spacing
    static constexpr int CELL_H     = 8;    // 7 glyph + 1 spacing
    static constexpr int X_ORIGIN   = 0;
    static constexpr int Y_ORIGIN   = 48;   // top of console area
    static constexpr int TAB_STOP   = 8;

    // Terminal personality. Selects how the conflicting C0 codes 0x0B/0x0C/0x1A
    // and ESC E are interpreted. Non-conflicting codes from both families
    // (ESC =, ESC T, ESC Y, 0x1E for ADM-3A; CSI sequences for VT-100) are
    // accepted in either mode.
    enum TermMode : uint8_t { TM_VT100 = 0, TM_ADM3A = 1 };

    bool begin(lgfx::LGFX_Device* lcd, uint16_t fg = 0xFFFF, uint16_t bg = 0x0000);
    void clear();                           // FF: clear + home
    void putc(char c);
    void puts(const char* s);
    void print(const char* s) { puts(s); }
    void println(const char* s) { puts(s); putc('\n'); }

    void setTerminalMode(TermMode m) { mode_ = m; }
    TermMode terminalMode() const { return mode_; }

    // Viewport (horizontal pan).
    void viewLeft(int cols = 8);            // pan view toward col 0
    void viewRight(int cols = 8);           // pan view toward col 79
    void viewHome()  { setView(0); }
    void setView(int col);
    int  view() const { return viewCol_; }

    // Vertical row span — by default shows all 24 buffer rows in the
    // 192 px console area. When the on-screen keyboard is open, the
    // caller can shrink the visible span (e.g. setViewRowSpan(18, 6)
    // shows just the last 6 rows in the top 48 px so the cursor stays
    // visible while the keyboard occupies the bottom 144 px).
    void setViewRowSpan(int topRow, int rowCount);
    int  viewRowTop()   const { return viewRowTop_; }
    int  viewRowCount() const { return viewRowsCount_; }

    // Colors apply to subsequent writes.
    void setColors(uint16_t fg, uint16_t bg) { fg_ = fg; bg_ = bg; }

    // Flush dirty lines to the display. Call from the display task/loop.
    void render();

    // Force full redraw on next render() (e.g., after viewport change).
    void markAllDirty();

    // Query cursor (for debugging).
    int cursorX() const { return cx_; }
    int cursorY() const { return cy_; }

private:
    lgfx::LGFX_Device* lcd_ = nullptr;
    lgfx::LGFX_Sprite  lineSpr_;        // 320x8 line buffer
    char buf_[ROWS][COLS];
    bool dirty_[ROWS];
    int  cx_ = 0;
    int  cy_ = 0;
    int  viewCol_       = 0;
    int  viewRowTop_    = 0;
    int  viewRowsCount_ = ROWS;
    uint16_t fg_ = 0xFFFF;
    uint16_t bg_ = 0x0000;

    enum ParseState : uint8_t {
        PS_GROUND, PS_ESC, PS_CSI,
        PS_ADM_CUP_ROW,   // saw ESC '='; next byte = row + 0x20
        PS_ADM_CUP_COL    // saw row byte; next byte = col + 0x20
    };
    static constexpr int MAX_PARAMS = 2;
    ParseState pstate_ = PS_GROUND;
    int   params_[MAX_PARAMS]    = {0, 0};
    bool  paramSeen_[MAX_PARAMS] = {false, false};
    int   paramCount_   = 0;
    int   csiParamIdx_  = 0;
    bool  csiIgnore_    = false;
    int   savedCx_      = 0;
    int   savedCy_      = 0;
    int   admCupRow_    = 0;
    TermMode mode_      = TM_VT100;

    void scrollUp();
    void newline();
    void writeCell(int row, int col, char c);
    void renderLine(int row);
    void drawGlyphIntoSprite(int sx, char c);

    void putcGround(uint8_t c);
    void parseEsc(uint8_t c);
    void parseCsi(uint8_t c);
    void parseAdmCupRow(uint8_t c);
    void parseAdmCupCol(uint8_t c);
    void dispatchCsi(uint8_t finalByte);
    int  csiParam(int i, int defaultVal) const;
    void cursorTo(int row, int col);
    void eraseInLine(int mode);
    void eraseInDisplay(int mode);
};
