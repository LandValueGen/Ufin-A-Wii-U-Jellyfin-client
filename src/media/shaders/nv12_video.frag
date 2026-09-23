// Ufin video shader (fragment/pixel stage) -- original, written for this
// project. Converts NV12 (the only format the Wii U's hardware H.264
// decoder, and therefore h264_wiiu, produces) to RGB on the GPU.
//
// Doing the YUV->RGB conversion here instead of with libswscale on the
// CPU is not a nicety: a 1280x720 NV12->RGBA sws_scale on the Wii U's
// PowerPC core costs more than a whole frame's time budget at 30 fps,
// and was the main reason the earlier RGBA-texture design could never
// have kept up even once it displayed. The GPU does this for free.
//
// tex_y  : R8 texture, full resolution   (luma)
// tex_uv : RG8 texture, half resolution  (.r = Cb/U, .g = Cr/V, interleaved)
//
// Coefficients are BT.709 limited range ("TV range", 16..235 luma), which
// is what Jellyfin's H.264 transcodes of HD content carry. Sampler
// binding indices are looked up by name at runtime (video_output.cpp),
// so the uniform names here must not change without updating that.
#version 420 core

layout(binding = 0) uniform sampler2D tex_y;
layout(binding = 1) uniform sampler2D tex_uv;

in vec2 v_uv;
out vec4 out_color;

void main() {
    float y = texture(tex_y, v_uv).r;
    vec2 uv = texture(tex_uv, v_uv).rg;

    float Y  = 1.164384 * (y - 16.0 / 255.0);
    float Cb = uv.r - 128.0 / 255.0;
    float Cr = uv.g - 128.0 / 255.0;

    vec3 rgb = vec3(
        Y + 1.792741 * Cr,
        Y - 0.213249 * Cb - 0.532909 * Cr,
        Y + 2.112402 * Cb
    );

    out_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
