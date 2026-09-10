// ui::Surface -- the tiny drawing abstraction the menu UI renders to.
//
// The Wii U side implements it on top of OSScreen (see
// os_screen_display.h), whose whole feature set is: clear, put a pixel,
// put a string of built-in 12x24 white glyphs at a text-grid position.
// That's enough for a clean panel-style UI (bands, highlights, progress
// bars) as long as the screens are laid out in terms of that text grid,
// which is what the helpers below are for.
//
// Keeping rendering behind this interface is also what lets the screens
// be exercised on a desktop machine (tests/host implements Surface over
// a plain pixel array), since nothing here depends on wut.

#pragma once
#include <cstdint>
#include <string>

namespace ui {

// Colours are 0xRRGGBBAA, matching OSScreen's pixel format.
namespace color {
    static const uint32_t BACKGROUND    = 0x101418FF;
    static const uint32_t HEADER        = 0x1F2A3AFF;
    static const uint32_t HEADER_ERROR  = 0x8B2635FF;
    static const uint32_t FOOTER        = 0x1A2230FF;
    static const uint32_t SELECTION     = 0x2F6FD6FF;
    static const uint32_t PANEL         = 0x1A2230FF;
    static const uint32_t TRACK         = 0x2A3340FF;
    static const uint32_t PROGRESS      = 0x4C9AFFFF;
    static const uint32_t SCROLL_THUMB  = 0x6B7C93FF;
    static const uint32_t WHITE         = 0xFFFFFFFF;
}

class Surface {
public:
    virtual ~Surface() {}

    virtual int width() const = 0;
    virtual int height() const = 0;

    virtual void clear(uint32_t rgba) = 0;

    // Filled axis-aligned rectangle; implementations must clip to the
    // surface.
    virtual void fillRect(int x, int y, int w, int h, uint32_t rgba) = 0;

    // Draws ASCII text at a text-grid cell (column, row). The OSScreen
    // font is white-on-transparent only, so anything behind it must be
    // dark enough to read. Text is NOT clipped by the implementation --
    // callers use fitText()/columns().
    virtual void drawText(int column, int row, const std::string& text) = 0;

    // OSScreen font grid: glyph cells are 12x24 px, the grid starts at
    // (50, 32). Same on TV and GamePad.
    static const int GRID_ORIGIN_X = 50;
    static const int GRID_ORIGIN_Y = 32;
    static const int CELL_W = 12;
    static const int CELL_H = 24;
    static const int GLYPH_H = 19;

    // Usable text grid, leaving the same margin on the right as on the
    // left. TV (1280x720): 98 columns x 28 rows. GamePad (854x480):
    // 62 columns x 18 rows.
    int columns() const { return (width() - 2 * GRID_ORIGIN_X) / CELL_W; }
    int rows() const { return (height() - GRID_ORIGIN_Y) / CELL_H; }

    // Pixel geometry of text cells, for drawing bands behind text.
    static int rowTop(int row) { return GRID_ORIGIN_Y + row * CELL_H - 3; }
    static int columnLeft(int column) { return GRID_ORIGIN_X + column * CELL_W; }

    // Fills the full-width band behind text row `row` (with a little
    // padding so glyphs don't touch the edge of the band).
    void fillRowBand(int row, uint32_t rgba, int inset = GRID_ORIGIN_X - 8) {
        fillRect(inset, rowTop(row), width() - 2 * inset, CELL_H, rgba);
    }
};

} // namespace ui
