// ui::Surface -- the tiny drawing abstraction the menu UI renders to.
//
// The Wii U side implements it on top of OSScreen (see
// os_screen_display.h), whose whole feature set is: clear, put a pixel,
// put a string of built-in white glyphs at a text-grid position. That's
// enough for a clean panel-style UI (bands, highlights, progress bars)
// as long as the screens are laid out in terms of that text grid, which
// is what the helpers below are for.
//
// The grid itself (where cell 0,0 is and how big a cell is) is NOT
// hardcoded: real hardware and Cemu put OSScreen text in different
// places with different glyph sizes, and a layout built for one draws
// highlights a row off and wraps long lines on the other. OSScreenDisplay
// measures the grid at start-up by drawing into its own framebuffer and
// looking at where the pixels landed (see os_screen_display.cpp), and
// hands the result to each surface via setGrid().
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
    static const uint32_t BACKGROUND    = 0x0F1217FF;
    static const uint32_t HEADER        = 0x1A2130FF;
    static const uint32_t HEADER_ERROR  = 0x8B2635FF;
    static const uint32_t ACCENT        = 0xAA5CC3FF; // Jellyfin purple
    static const uint32_t FOOTER        = 0x161C27FF;
    static const uint32_t SELECTION     = 0x0B7FB0FF; // Jellyfin blue, dimmed for white text
    static const uint32_t PANEL         = 0x1A2130FF;
    static const uint32_t TRACK         = 0x2A3340FF;
    static const uint32_t PROGRESS      = 0x00A4DCFF;
    static const uint32_t SCROLL_THUMB  = 0x6B7C93FF;
    static const uint32_t WHITE         = 0xFFFFFFFF;
}

// Pixel geometry of the OSScreen text grid: text drawn at (column, row)
// fills the cell starting at (originX + column*cellW, originY + row*cellH).
// originY is the top of the cell (a little above the glyphs), so a band
// filled over the cell sits behind the text.
struct GridMetrics {
    int originX = 50;
    int originY = 29;
    int cellW = 12;
    int cellH = 24;
    bool measured = false; // true if taken from the real framebuffer
};

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

    const GridMetrics& grid() const { return grid_; }
    void setGrid(const GridMetrics& g) { grid_ = g; }

    int cellW() const { return grid_.cellW; }
    int cellH() const { return grid_.cellH; }

    // First usable text column. If the font grid starts right at the
    // screen edge (Cemu does this), skip enough columns to leave a
    // margin, so text never touches the bezel/overscan area.
    int leftColumn() const {
        const int minMargin = 24;
        if (grid_.originX >= minMargin) return 0;
        return (minMargin - grid_.originX + grid_.cellW - 1) / grid_.cellW;
    }

    // Usable columns from leftColumn(), leaving the same margin on the
    // right as on the left. Nothing longer than this may be drawn on a
    // row, or the OSScreen font wraps it back over the same row.
    int columns() const {
        int leftPx = columnLeft(leftColumn());
        int n = (width() - 2 * leftPx) / grid_.cellW;
        return n < 1 ? 1 : n;
    }
    int rows() const {
        int n = (height() - grid_.originY) / grid_.cellH;
        return n < 1 ? 1 : n;
    }

    // Pixel geometry of text cells, for drawing bands behind text.
    int rowTop(int row) const { return grid_.originY + row * grid_.cellH; }
    int columnLeft(int column) const { return grid_.originX + column * grid_.cellW; }

    // Left/right pixel edges of the usable text area.
    int contentLeft() const { return columnLeft(leftColumn()); }
    int contentRight() const { return contentLeft() + columns() * grid_.cellW; }

    // Fills the band behind text row `row` across the usable text area,
    // with a little padding so glyphs don't touch the band's edges.
    void fillRowBand(int row, uint32_t rgba) {
        const int pad = 8;
        fillRect(contentLeft() - pad, rowTop(row), columns() * grid_.cellW + 2 * pad, grid_.cellH, rgba);
    }

private:
    GridMetrics grid_;
};

} // namespace ui
