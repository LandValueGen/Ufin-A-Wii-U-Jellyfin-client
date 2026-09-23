// OSScreenDisplay -- owns the OSScreen framebuffers for the TV and the
// GamePad and exposes each as a ui::Surface, so the menu screens can be
// drawn identically to both. This is the only file in ui/ that touches
// wut.
//
// OSScreen is used for menus because it needs no shader pipeline and
// coexists with plain text output; it is fully shut down (shutdown())
// before GX2 video playback starts and re-created afterwards, since both
// drive the same display hardware and can't be active at once.

#pragma once
#include "surface.h"

#include <coreinit/screen.h>
#include <cstdint>

class OSScreenSurface : public ui::Surface {
public:
    OSScreenSurface(OSScreenID id, int width, int height)
        : id_(id), width_(width), height_(height) {}

    int width() const override { return width_; }
    int height() const override { return height_; }
    void clear(uint32_t rgba) override;
    void fillRect(int x, int y, int w, int h, uint32_t rgba) override;
    void drawText(int column, int row, const std::string& text) override;

private:
    OSScreenID id_;
    int width_;
    int height_;
};

class OSScreenDisplay {
public:
    // Initialises OSScreen and allocates both framebuffers. Safe to call
    // again after shutdown().
    bool init();
    void shutdown();
    bool isActive() const { return active_; }

    ui::Surface& tv() { return tv_; }
    ui::Surface& drc() { return drc_; }

    // Runs `draw` once per screen, then flushes and flips both. `draw`
    // gets a ui::Surface&.
    template <typename DrawFn>
    void render(DrawFn draw) {
        if (!active_) return;
        draw(tv_);
        draw(drc_);
        flip();
    }

private:
    bool active_ = false;
    void* tvBuffer_ = nullptr;
    void* drcBuffer_ = nullptr;
    uint32_t tvSize_ = 0;
    uint32_t drcSize_ = 0;
    OSScreenSurface tv_{SCREEN_TV, 1280, 720};
    OSScreenSurface drc_{SCREEN_DRC, 854, 480};
    bool gridMeasured_ = false;
    ui::GridMetrics tvGrid_;
    ui::GridMetrics drcGrid_;

    void flip();
};
