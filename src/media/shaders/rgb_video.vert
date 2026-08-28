// Ufin video shader (vertex stage) -- original, written for this project.
// Fullscreen-quad passthrough: takes a clip-space position for each of
// the 4 corners of the screen and derives the matching texture UV
// coordinate, so the whole screen gets covered by the video texture.
#version 420 core

layout(location = 0) in vec2 in_pos;

layout(location = 0) out vec2 v_uv;

void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    v_uv = in_pos * 0.5 + 0.5;
    v_uv.y = 1.0 - v_uv.y; // flip: texture origin is top-left, clip space is bottom-left
}
