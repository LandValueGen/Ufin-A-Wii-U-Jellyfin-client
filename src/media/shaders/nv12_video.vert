// Ufin video shader (vertex stage) -- original, written for this project.
// Draws the aspect-fitted video quad: each vertex carries its clip-space
// position and its texture coordinate directly (see
// VideoOutput::buildQuad in video_output.cpp), so letterboxing /
// pillarboxing is just a matter of where the four corners land -- the
// shader itself does no aspect math.
//
// Attribute names ("in_pos", "in_uv") are looked up by name at runtime
// via WHBGfxInitShaderAttribute, so they must match video_output.cpp.
#version 420 core

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;

out vec2 v_uv;

void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    v_uv = in_uv;
}
