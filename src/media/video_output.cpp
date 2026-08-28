#include "video_output.h"
#include "shaders/rgb_video_shader.h"

#include <cstdio>
#include <cstring>
#include <coreinit/debug.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/cache.h>

VideoOutput::VideoOutput() {}

VideoOutput::~VideoOutput() {
    shutdown();
}

bool VideoOutput::loadShader() {
    // rgb_video_shader_data/_size come from the auto-generated header,
    // produced at build time by compiling src/media/shaders/rgb_video.vert
    // + rgb_video.frag with glslcompiler.elf (see CMakeLists.txt) and
    // converting the resulting .gsh with tools/bin2h.py.
    if (!WHBGfxLoadGFDShaderGroup(&shader_, 0, rgb_video_shader_data)) {
        snprintf(last_error_, sizeof(last_error_), "WHBGfxLoadGFDShaderGroup failed");
        OSReport("Ufin: WHBGfxLoadGFDShaderGroup failed\n");
        return false;
    }

    // "in_pos" must match the vertex shader's declared input name
    // (see rgb_video.vert: `layout(location = 0) in vec2 in_pos;`).
    if (!WHBGfxInitShaderAttribute(&shader_, "in_pos", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32)) {
        snprintf(last_error_, sizeof(last_error_), "WHBGfxInitShaderAttribute failed");
        OSReport("Ufin: WHBGfxInitShaderAttribute failed\n");
        return false;
    }

    if (!WHBGfxInitFetchShader(&shader_)) {
        snprintf(last_error_, sizeof(last_error_), "WHBGfxInitFetchShader failed");
        OSReport("Ufin: WHBGfxInitFetchShader failed\n");
        return false;
    }
    OSReport("Ufin: shader loaded ok\n");
    return true;
}

bool VideoOutput::createTexture() {
    // Double-buffered GX2R-managed surfaces. Confirmed via CafeMP's own
    // working renderer (VideoPlane::tex[2], plane_write_idx ^= 1) that
    // this is required for a texture rewritten every frame: write into
    // one buffer while drawing from the other (the previous, fully-
    // written frame), then swap roles. A single texture locked/rewritten
    // 30x/sec risks the GPU sampling it mid-write -- consistent with
    // everything observed: correct data confirmed in GPU-visible memory,
    // yet nothing displayed, plus growing instability over many cycles.
    texture_flags_ = (GX2RResourceFlags)(
        GX2R_RESOURCE_BIND_TEXTURE |
        GX2R_RESOURCE_USAGE_CPU_WRITE |
        GX2R_RESOURCE_USAGE_GPU_READ
    );

    for (int b = 0; b < 2; b++) {
        GX2Surface& surf = texture_[b].surface;
        surf.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        surf.width = (uint32_t)width_;
        surf.height = (uint32_t)height_;
        surf.depth = 1;
        surf.mipLevels = 1;
        surf.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        surf.aa = GX2_AA_MODE1X;
        surf.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
        surf.resourceFlags = texture_flags_;

        if (!GX2RCreateSurface(&surf, texture_flags_)) {
            snprintf(last_error_, sizeof(last_error_), "GX2RCreateSurface failed (buffer %d)", b);
            OSReport("Ufin: GX2RCreateSurface failed, buffer %d\n", b);
            return false;
        }

        texture_[b].viewFirstMip = 0;
        texture_[b].viewNumMips = 1;
        texture_[b].viewFirstSlice = 0;
        texture_[b].viewNumSlices = 1;
        texture_[b].compMap = 0x00010203; // identity R,G,B,A mapping

        GX2InitTextureRegs(&texture_[b]);

        // Blank-fill both buffers up front (black, alpha opaque) so the
        // very first draw -- before any real frame has been written --
        // doesn't show uninitialized memory.
        uint8_t* px = (uint8_t*)GX2RLockSurfaceEx(&surf, 0, GX2R_RESOURCE_BIND_NONE);
        if (px) {
            memset(px, 0, surf.imageSize);
            GX2RUnlockSurfaceEx(&surf, 0, GX2R_RESOURCE_BIND_NONE);
        }
    }

    GX2InitSampler(&sampler_, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);

    OSReport("Ufin: double-buffered textures created, imageSize=%u pitch=%u\n",
             texture_[0].surface.imageSize, texture_[0].surface.pitch);
    return true;
}

bool VideoOutput::createQuad() {
    // Fullscreen quad as a triangle strip: 4 corners in clip space
    // (-1..1 on both axes). rgb_video.vert derives the UV coordinate
    // from this position directly, so no separate UV buffer is needed.
    static const float positions[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    quad_buffer_size_ = sizeof(positions);
    quad_buffer_ = MEMAllocFromDefaultHeapEx(quad_buffer_size_, GX2_VERTEX_BUFFER_ALIGNMENT);
    if (!quad_buffer_) {
        snprintf(last_error_, sizeof(last_error_), "quad buffer allocation failed");
        OSReport("Ufin: quad buffer alloc failed\n");
        return false;
    }

    memcpy(quad_buffer_, positions, sizeof(positions));
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, quad_buffer_, quad_buffer_size_);

    OSReport("Ufin: quad vertex buffer created ok\n");
    return true;
}

bool VideoOutput::init(int width, int height) {
    width_ = width;
    height_ = height;
    OSReport("Ufin: VideoOutput::init (GX2, double-buffered) %dx%d\n", width, height);

    if (!WHBGfxInit()) {
        snprintf(last_error_, sizeof(last_error_), "WHBGfxInit failed");
        OSReport("Ufin: WHBGfxInit failed\n");
        return false;
    }
    gfx_initialized_ = true;

    if (!loadShader()) return false;
    if (!createTexture()) return false;
    if (!createQuad()) return false;

    sws_ctx_ = sws_getContext(width_, height_, AV_PIX_FMT_NV12,
                               width_, height_, AV_PIX_FMT_RGBA,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        snprintf(last_error_, sizeof(last_error_), "sws_getContext failed");
        OSReport("Ufin: sws_getContext failed\n");
        return false;
    }

    rgba_linesize_ = width_ * 4;
    rgba_buffer_.resize((size_t)rgba_linesize_ * height_);

    writeIndex_ = 0;

    OSReport("Ufin: VideoOutput::init complete\n");
    return true;
}

void VideoOutput::drawFrame(int readIndex, int targetWidth, int targetHeight) {
    // GX2's fixed-function state (blend, culling, depth test, viewport,
    // scissor) isn't reset to sane defaults automatically -- set all of
    // this explicitly every draw (confirmed necessary via CafeMP).
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);
    GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO,
                        GX2_BLEND_COMBINE_MODE_ADD, FALSE,
                        GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
    // Target-specific resolution, not the video's decode dimensions --
    // TV (1280x720) and DRC/gamepad (854x480) are different physical
    // targets. Using the video's fixed 1280x720 for both (confirmed via
    // CafeMP's video_render_common using display_get().width/height per
    // target instead) meant the DRC pass had a viewport/scissor larger
    // than its actual surface, which could easily explain broken or
    // invisible output on that target.
    GX2SetViewport(0, 0, (float)targetWidth, (float)targetHeight, 0.0f, 1.0f);
    GX2SetScissor(0, 0, (uint32_t)targetWidth, (uint32_t)targetHeight);

    uint32_t samplerLocation = shader_.pixelShader->samplerVars[0].location;

    GX2SetFetchShader(&shader_.fetchShader);
    GX2SetVertexShader(shader_.vertexShader);
    GX2SetPixelShader(shader_.pixelShader);

    GX2SetPixelTexture(&texture_[readIndex], samplerLocation);
    GX2SetPixelSampler(&sampler_, samplerLocation);

    GX2SetAttribBuffer(0, quad_buffer_size_, sizeof(float) * 2, quad_buffer_);

    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4, 0, 1);
}

void VideoOutput::renderTestPattern() {
    // Ensure the GPU has actually finished any previous draw that might
    // still be reading this buffer before we overwrite it -- GX2R's
    // lock/unlock alone doesn't appear to guarantee this on its own,
    // which is the likely explanation for both the flickering here and
    // real video never displaying (same race, different visibility).
    GX2DrawDone();

    GX2Surface& surf = texture_[writeIndex_].surface;
    uint8_t* dst = (uint8_t*)GX2RLockSurfaceEx(&surf, 0, GX2R_RESOURCE_BIND_NONE);
    if (!dst) {
        OSReport("Ufin: renderTestPattern -- GX2RLockSurfaceEx returned null\n");
        return;
    }

    uint32_t pitchBytes = surf.pitch * 4;
    for (int row = 0; row < height_; row++) {
        uint8_t* rowPtr = dst + row * pitchBytes;
        for (int col = 0; col < width_; col++) {
            rowPtr[col * 4 + 0] = 255; // R
            rowPtr[col * 4 + 1] = 0;   // G
            rowPtr[col * 4 + 2] = 255; // B
            rowPtr[col * 4 + 3] = 255; // A
        }
    }
    // Explicit flush -- see the longer explanation in renderFrame() for
    // why this matters. This path writes bytes directly rather than
    // copying from a source buffer, so there's no OSBlockMove call to
    // carry the flush; do it explicitly instead.
    DCFlushRange(dst, surf.imageSize);
    GX2RUnlockSurfaceEx(&surf, 0, GX2R_RESOURCE_BIND_NONE);

    int readIndex = writeIndex_ ^ 1;

    WHBGfxBeginRender();
    WHBGfxBeginRenderTV();
    drawFrame(readIndex, 1280, 720);
    WHBGfxFinishRenderTV();
    WHBGfxBeginRenderDRC();
    drawFrame(readIndex, 854, 480);
    WHBGfxFinishRenderDRC();
    WHBGfxFinishRender();

    writeIndex_ ^= 1;
}

void VideoOutput::renderFrame(AVFrame* frame) {
    if (!sws_ctx_) {
        OSReport("Ufin: renderFrame called but sws_ctx_ is null -- early return\n");
        return;
    }

    static int callCount = 0;
    callCount++;
    if (callCount <= 5 || callCount % 30 == 0) {
        OSReport("Ufin: renderFrame call #%d\n", callCount);
    }

    uint8_t* dstData[1] = { rgba_buffer_.data() };
    int dstLinesize[1] = { rgba_linesize_ };
    sws_scale(sws_ctx_, frame->data, frame->linesize, 0, height_, dstData, dstLinesize);

    // See renderTestPattern() for why this is here -- ensures the GPU
    // has finished any previous draw reading this buffer before we
    // overwrite it.
    GX2DrawDone();

    GX2Surface& surf = texture_[writeIndex_].surface;
    uint8_t* dst = (uint8_t*)GX2RLockSurfaceEx(&surf, 0, GX2R_RESOURCE_BIND_NONE);
    if (!dst) {
        OSReport("Ufin: renderFrame -- GX2RLockSurfaceEx returned null\n");
        return;
    }

    uint32_t pitchBytes = surf.pitch * 4;
    if (pitchBytes == (uint32_t)rgba_linesize_) {
        // OSBlockMove with flush=TRUE, not memcpy -- confirmed via
        // CafeMP's own upload_plane() that a plain memcpy here doesn't
        // guarantee the GPU actually sees the write. memcpy only
        // operates through the CPU's own cache; the GPU reads via a
        // different path and could see stale (pre-write) data without
        // an explicit flush, even though a CPU-side readback right
        // after would show the correct value (hitting the CPU cache).
        // This is consistent with everything we observed: correct data
        // confirmed from the CPU's perspective, never actually visible.
        OSBlockMove(dst, rgba_buffer_.data(), (uint32_t)rgba_buffer_.size(), TRUE);
    } else {
        for (int row = 0; row < height_; row++) {
            OSBlockMove(dst + row * pitchBytes, rgba_buffer_.data() + row * rgba_linesize_,
                        (uint32_t)rgba_linesize_, TRUE);
        }
    }
    GX2RUnlockSurfaceEx(&surf, 0, GX2R_RESOURCE_BIND_NONE);

    // Draw from the OTHER buffer -- the previous frame, already fully
    // written and not being touched by this frame's CPU write above.
    int readIndex = writeIndex_ ^ 1;

    WHBGfxBeginRender();
    WHBGfxBeginRenderTV();
    drawFrame(readIndex, 1280, 720);
    WHBGfxFinishRenderTV();
    WHBGfxBeginRenderDRC();
    drawFrame(readIndex, 854, 480);
    WHBGfxFinishRenderDRC();
    WHBGfxFinishRender();

    writeIndex_ ^= 1;
}

void VideoOutput::shutdown() {
    if (sws_ctx_) { sws_freeContext(sws_ctx_); sws_ctx_ = nullptr; }
    for (int b = 0; b < 2; b++) {
        if (texture_[b].surface.imageSize > 0) {
            GX2RDestroySurfaceEx(&texture_[b].surface, GX2R_RESOURCE_BIND_NONE);
        }
    }
    if (quad_buffer_) {
        MEMFreeToDefaultHeap(quad_buffer_);
        quad_buffer_ = nullptr;
    }
    if (gfx_initialized_) {
        WHBGfxShutdown();
        gfx_initialized_ = false;
    }
}
