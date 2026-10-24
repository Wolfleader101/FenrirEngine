// Fenrir/2D.glsl: binding set 0 of every 2D shader, supplied by the 2D target (ADR-0027). A 2D shader's own
// parameters are in set 1: its MaterialParams block at binding 0, then its textures. FenrirShaderCompiler knows a 2D
// shader by this block, and reflects set 1 as its material set.
#ifndef FENRIR_2D_GLSL
#define FENRIR_2D_GLSL

layout(set = 0, binding = 0, std140) uniform Target2D
{
    // From local logical coordinates (top-left origin, Y down) to clip space.
    mat4 localToClip;
    // xy: the target's size in pixels. zw unused.
    vec4 pixelSize;
    // x: the target's content scale (pixels per logical unit). yzw unused.
    vec4 contentScale;
} target2D;

#endif
