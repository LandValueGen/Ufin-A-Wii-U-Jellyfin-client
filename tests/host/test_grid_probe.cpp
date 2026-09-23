// ui::measureGrid against a fake OSScreen: a double-buffered framebuffer
// with a configurable pitch, work-buffer half, text origin and glyph
// size. Checks the measured grid puts every glyph's ink inside the cell
// the layout thinks it's in -- which is what keeps selection bands under
// the right row on both real hardware and Cemu.
#include "check.h"
#include "ui/grid_probe.h"

#include <vector>

struct FakeScreen {
    int width, height, pitch;
    int workHalf;               // which half of the buffer OSScreen draws into
    int originX, originY;       // top-left of cell (0,0)
    int cellW, cellH;
    int inkLeft, inkTop, inkW, inkH; // glyph ink box inside a cell
    std::vector<uint32_t> buf;

    FakeScreen(int w, int h, int p, int half, int ox, int oy, int cw, int ch)
        : width(w), height(h), pitch(p), workHalf(half), originX(ox), originY(oy), cellW(cw), cellH(ch),
          inkLeft(1), inkTop(ch / 6), inkW(cw - 2), inkH(ch * 2 / 3),
          buf((size_t)p * h * 2, 0) {}

    uint32_t* work() { return buf.data() + (size_t)workHalf * pitch * height; }
    void clear() { std::fill(work(), work() + (size_t)pitch * height, 0u); }
    void pixel(int x, int y) {
        if (x >= 0 && y >= 0 && x < width && y < height) work()[(size_t)y * pitch + x] = 0xFFFFFFFF;
    }
    void text(int col, int row, const char* s) {
        for (int i = 0; s[i]; i++) {
            int x0 = originX + (col + i) * cellW + inkLeft;
            int y0 = originY + row * cellH + inkTop;
            for (int y = 0; y < inkH; y++)
                for (int x = 0; x < inkW; x++) pixel(x0 + x, y0 + y);
        }
    }
    ui::GridProbeOps ops() {
        ui::GridProbeOps o;
        o.clear = [this] { clear(); };
        o.putPixel = [this](int x, int y) { pixel(x, y); };
        o.putText = [this](int c, int r, const char* s) { text(c, r, s); };
        return o;
    }
};

static void checkScreen(FakeScreen fs) {
    ui::GridMetrics g;
    CHECK(ui::measureGrid(fs.buf.data(), fs.buf.size() * 4, fs.width, fs.ops(), g));
    CHECK(g.measured);
    CHECK_EQ(g.cellW, fs.cellW);
    CHECK_EQ(g.cellH, fs.cellH);
    // Ink of any cell must sit inside the cell the layout computes.
    for (int row = 0; row < 5; row++) {
        int inkTopPx = fs.originY + row * fs.cellH + fs.inkTop;
        int inkBottomPx = inkTopPx + fs.inkH - 1;
        int cellTop = g.originY + row * g.cellH;
        CHECK(inkTopPx >= cellTop);
        CHECK(inkBottomPx < cellTop + g.cellH);
    }
    for (int col = 0; col < 5; col++) {
        int inkLeftPx = fs.originX + col * fs.cellW + fs.inkLeft;
        int cellLeft = g.originX + col * g.cellW;
        CHECK(inkLeftPx >= cellLeft);
        CHECK(inkLeftPx + fs.inkW - 1 < cellLeft + g.cellW);
    }
    // Probing leaves the work buffer clean.
    bool clean = true;
    for (uint32_t v : fs.buf) if (v) clean = false;
    CHECK(clean);
}

int main() {
    checkScreen(FakeScreen(1280, 720, 1280, 0, 50, 29, 12, 24)); // "hardware" defaults, first half
    checkScreen(FakeScreen(1280, 720, 1280, 1, 50, 29, 12, 24)); // same, drawing into second half
    checkScreen(FakeScreen(1280, 720, 1280, 1, 0, 0, 16, 24));   // Cemu-like: grid at the edge
    checkScreen(FakeScreen(854, 480, 896, 0, 0, 0, 16, 24));     // GamePad with padded pitch
    checkScreen(FakeScreen(1920, 1080, 1920, 1, 70, 40, 18, 36)); // 1080p

    // A screen that draws nothing: measurement fails and leaves defaults.
    {
        std::vector<uint32_t> buf(1280 * 720 * 2, 0);
        ui::GridProbeOps o;
        o.clear = [] {};
        o.putPixel = [](int, int) {};
        o.putText = [](int, int, const char*) {};
        ui::GridMetrics g;
        CHECK(!ui::measureGrid(buf.data(), buf.size() * 4, 1280, o, g));
        CHECK(!g.measured);
        CHECK_EQ(g.cellW, 12);
    }
    return check::finish("test_grid_probe");
}
