// Measures where an OSScreen-style text grid actually lands in a
// framebuffer, by drawing probes and looking at which pixels changed.
// Kept free of wut so the host tests can run it against a fake screen
// (tests/host/test_grid_probe.cpp); os_screen_display.cpp wires it to
// the real OSScreen calls.
#pragma once
#include "surface.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace ui {

struct GridProbeOps {
    std::function<void()> clear;                               // clear work buffer to 0
    std::function<void(int x, int y)> putPixel;                // one non-zero pixel
    std::function<void(int col, int row, const char*)> putText;
};

namespace detail {

struct InkBox {
    int minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
    bool empty() const { return maxX < 0; }
};

inline long firstLitWord(const uint32_t* words, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (words[i] != 0) return (long)i;
    }
    return -1;
}

inline InkBox scanInk(const uint32_t* words, size_t count, long base, int pitch, int w, int h) {
    InkBox box;
    for (int y = 0; y < h; y++) {
        size_t rowStart = (size_t)base + (size_t)y * (size_t)pitch;
        if (rowStart + (size_t)w > count) break;
        for (int x = 0; x < w; x++) {
            if (words[rowStart + x] != 0) {
                if (x < box.minX) box.minX = x;
                if (y < box.minY) box.minY = y;
                if (x > box.maxX) box.maxX = x;
                if (y > box.maxY) box.maxY = y;
            }
        }
    }
    return box;
}

} // namespace detail

// `buffer`/`sizeBytes` is the whole OSScreen allocation (both halves of
// the double buffer), which must be all-zero except for what the ops
// draw. Returns false (leaving `out` alone) if the result looks wrong.
inline bool measureGrid(const void* buffer, size_t sizeBytes, int width, const GridProbeOps& ops,
                        GridMetrics& out) {
    const uint32_t* words = (const uint32_t*)buffer;
    const size_t count = sizeBytes / 4;
    const char* probe = "Mg|"; // wide, descender, tall: covers the ink extent

    ops.clear();
    ops.putPixel(0, 0);
    long p00 = detail::firstLitWord(words, count);
    ops.clear();
    ops.putPixel(0, 1);
    long p01 = detail::firstLitWord(words, count);
    if (p00 < 0 || p01 <= p00) return false;
    int pitch = (int)(p01 - p00);
    if (pitch < width || pitch > 4096) return false;

    const int scanW = 256, scanH = 192;
    ops.clear();
    ops.putText(0, 0, probe);
    detail::InkBox a = detail::scanInk(words, count, p00, pitch, scanW, scanH);
    ops.clear();
    ops.putText(1, 1, probe);
    detail::InkBox b = detail::scanInk(words, count, p00, pitch, scanW, scanH);
    ops.clear();
    if (a.empty() || b.empty()) return false;

    int cellW = b.minX - a.minX;
    int cellH = b.minY - a.minY;
    if (cellW < 6 || cellW > 40 || cellH < 10 || cellH > 48) return false;

    int inkH = a.maxY - a.minY + 1;
    int topPad = (cellH - inkH) / 2;
    if (topPad < 0) topPad = 0;

    out.cellW = cellW;
    out.cellH = cellH;
    out.originX = a.minX > 0 ? a.minX - 1 : 0;
    out.originY = a.minY > topPad ? a.minY - topPad : 0;
    out.measured = true;
    return true;
}

} // namespace ui
