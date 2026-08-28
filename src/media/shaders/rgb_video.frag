// Ufin video shader (fragment/pixel stage) -- original, written for this
// project. Plain RGB passthrough: video_output.cpp already converts
// decoded NV12 frames to RGB24 on the CPU via libswscale before this
// shader ever runs, so no YUV math happens here -- this just samples
// and outputs the already-converted texture.
#version 420 core

layout(binding = 0) uniform sampler2D tex_rgb;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(texture(tex_rgb, v_uv).rgb, 1.0);
}
