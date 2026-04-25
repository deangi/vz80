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

    bool begin(lgfx::LGFX_Device* lcd, uint16_t fg = 0xFFFF, uint16_t bg = 0x0000);
    void clear();                           // FF: clear + home
    void putc(char c);
    void puts(const char* s);
    void print(const char* s) { puts(s); }
    void println(const char* s) { puts(s); putc('\n'); }

    // Viewport (horizontal pan).
    void viewLeft(int cols = 8);            // pan view toward col 0
    void viewRight(int cols = 8);           // pan view toward col 79
    void viewHome()  { setView(0); }
    void setView(int col);
    int  view() const { return viewCol_; }

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
    int  viewCol_ = 0;
    uint16_t fg_ = 0xFFFF;
    uint16_t bg_ = 0x0000;

    void scrollUp();
    void newline();
    void writeCell(int row, int col, char c);
    void renderLine(int row);
    void drawGlyphIntoSprite(int sx, char c);
};
