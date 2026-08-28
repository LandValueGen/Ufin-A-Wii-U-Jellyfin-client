// VideoOutput -- takes decoded video AVFrames and puts them on screen.
//
// This is a from-scratch raw-GX2 renderer, replacing an earlier SDL2-
// based implementation. SDL2's SDL_Renderer/SDL_Texture path was
// confirmed working end-to-end (every call succeeded, correct frame
// data uploaded) but never actually produced visible output on Wii U --
// strong evidence SDL2's high-level texture rendering has a real gap on
// this particular platform port. CafeMP (an existing, working Wii U
// media player) sidesteps this the same way: use SDL2 only for
// window/input/audio, and do actual GPU drawing via raw GX2 + a custom
// shader. This class follows that same architecture, using WHBGfx (a
// companion helper library within wut, same family as WHBProcInit) for
// GX2 context setup instead of SDL2's renderer.
//
// NOTE: this is a first draft against an API surface (raw GX2/WHBGfx)
// we have not compile-tested at all -- treat function names, struct
// fields, and call ordering below as a best-effort first attempt, not a
// verified-correct reference. Expect a real debugging round.
//
// Pixel format: decoded frames are NV12 (h264_wiiu's native output).
// This class converts them to RGBA32 on the CPU via libswscale (the
// same conversion approach already proven working with the old SDL2
// texture path -- RGBA rather than RGB24 this time because GX2/GPU
// texture formats are 4-byte-aligned; there's no real hardware format
// for tightly-packed 24-bit RGB). The GPU-side shader (see
// src/media/shaders/rgb_video.frag) is then a plain passthrough with no
// YUV math at all -- conversion happens entirely before the GPU is
// involved.

#pragma once
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <whb/gfx.h>
#include <gx2/mem.h>
#include <gx2/texture.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/registers.h>
#include <gx2/surface.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>
#include <gx2r/surface.h>

#include <vector>

class VideoOutput {
public:
    VideoOutput();
    ~VideoOutput();

    // width/height should match Decoder::videoWidth()/videoHeight().
    bool init(int width, int height);

    // Converts the frame from NV12 to RGBA32 (via swscale), uploads it
    // to the GPU texture, and draws + presents a fullscreen quad.
    // Assumes frame->format == AV_PIX_FMT_NV12.
    // Fills the texture with a solid color and draws+presents it via the
    // exact same pipeline as renderFrame(), but with no decode, no
    // swscale, no video data involved at all -- isolates whether the
    // fundamental GX2 shader/draw/present machinery works, decoupled
    // from everything video-specific. Diagnostic only.
    void renderTestPattern();

    void renderFrame(AVFrame* frame);

    void shutdown();

    const char* lastError() const { return last_error_; }

private:
    int width_ = 0;
    int height_ = 0;
    char last_error_[256] = {0};

    bool gfx_initialized_ = false;

    SwsContext* sws_ctx_ = nullptr;
    std::vector<uint8_t> rgba_buffer_;
    int rgba_linesize_ = 0;

    // Double-buffered: write the new frame into one texture while
    // drawing from the other (the previous, already-fully-written
    // frame), then swap roles each frame. Confirmed via CafeMP's own
    // working renderer (VideoPlane::tex[2], plane_write_idx ^= 1) that
    // this is necessary -- a single texture locked/rewritten 30x/sec
    // risks the GPU sampling it mid-write, which is consistent with
    // everything we saw: correct data confirmed in GPU-visible memory,
    // yet nothing displayed.
    GX2Texture texture_[2]{};
    int writeIndex_ = 0;
    GX2RResourceFlags texture_flags_{};
    GX2Sampler sampler_{};
    void* quad_buffer_ = nullptr;
    uint32_t quad_buffer_size_ = 0;
    WHBGfxShaderGroup shader_{};

    bool loadShader();
    bool createTexture();
    bool createQuad();
    void drawFrame(int readIndex, int targetWidth, int targetHeight); // draws + samples texture_[readIndex] into a target of the given resolution
};
