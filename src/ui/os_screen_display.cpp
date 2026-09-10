#include "os_screen_display.h"

#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>

void OSScreenSurface::clear(uint32_t rgba) {
    OSScreenClearBufferEx(id_, rgba);
}

void OSScreenSurface::fillRect(int x, int y, int w, int h, uint32_t rgba) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > width_ ? width_ : x + w;
    int y1 = y + h > height_ ? height_ : y + h;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            OSScreenPutPixelEx(id_, (uint32_t)px, (uint32_t)py, rgba);
        }
    }
}

void OSScreenSurface::drawText(int column, int row, const std::string& text) {
    if (column < 0 || row < 0 || text.empty()) return;
    OSScreenPutFontEx(id_, (uint32_t)column, (uint32_t)row, text.c_str());
}

// OSScreen's TV framebuffer is 1280x720 on a 720p (or 480p) TV. The
// buffer size is the one thing the API tells us about the mode, so use
// it to spot a 1080p framebuffer rather than assuming.
static void tvDimensionsFromBufferSize(uint32_t bytes, int& width, int& height) {
    // Sizes are for two buffers of 4 bytes per pixel.
    if (bytes >= 1920u * 1080u * 4u * 2u) {
        width = 1920;
        height = 1080;
    } else {
        width = 1280;
        height = 720;
    }
}

bool OSScreenDisplay::init() {
    if (active_) return true;

    OSScreenInit();

    tvSize_ = OSScreenGetBufferSizeEx(SCREEN_TV);
    drcSize_ = OSScreenGetBufferSizeEx(SCREEN_DRC);

    tvBuffer_ = MEMAllocFromDefaultHeapEx(tvSize_, 0x100);
    drcBuffer_ = MEMAllocFromDefaultHeapEx(drcSize_, 0x100);
    if (!tvBuffer_ || !drcBuffer_) {
        if (tvBuffer_) MEMFreeToDefaultHeap(tvBuffer_);
        if (drcBuffer_) MEMFreeToDefaultHeap(drcBuffer_);
        tvBuffer_ = drcBuffer_ = nullptr;
        OSScreenShutdown();
        return false;
    }

    OSScreenSetBufferEx(SCREEN_TV, tvBuffer_);
    OSScreenSetBufferEx(SCREEN_DRC, drcBuffer_);

    OSScreenEnableEx(SCREEN_TV, TRUE);
    OSScreenEnableEx(SCREEN_DRC, TRUE);

    int tvW = 1280, tvH = 720;
    tvDimensionsFromBufferSize(tvSize_, tvW, tvH);
    tv_ = OSScreenSurface(SCREEN_TV, tvW, tvH);
    drc_ = OSScreenSurface(SCREEN_DRC, 854, 480);

    active_ = true;
    return true;
}

void OSScreenDisplay::flip() {
    DCFlushRange(tvBuffer_, tvSize_);
    DCFlushRange(drcBuffer_, drcSize_);
    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);
}

void OSScreenDisplay::shutdown() {
    if (!active_) return;
    OSScreenEnableEx(SCREEN_TV, FALSE);
    OSScreenEnableEx(SCREEN_DRC, FALSE);
    // Fully release OSScreen before GX2 takes over the display -- both
    // drive the same hardware, and leaving OSScreen half-alive under
    // WHBGfxInit caused hard hangs. init() re-creates it afterwards.
    OSScreenShutdown();
    if (tvBuffer_) MEMFreeToDefaultHeap(tvBuffer_);
    if (drcBuffer_) MEMFreeToDefaultHeap(drcBuffer_);
    tvBuffer_ = nullptr;
    drcBuffer_ = nullptr;
    active_ = false;
}
