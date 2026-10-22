// Fenrir/Frame.glsl: binding set 0 of every material shader, the frame (ADR-0024). Written once per frame by the
// renderer, which declares the same layout in C++ and checks it against a shader's reflection in debug builds.
#ifndef FENRIR_FRAME_GLSL
#define FENRIR_FRAME_GLSL

layout(set = 0, binding = 0, std140) uniform FrameData
{
    // xyz: the way the directional light travels, in world space (its entity's world -Z), unit length. w unused.
    vec4 lightDirection;
    // rgb: the directional light's colour times its intensity, linear; black with no light. a unused.
    vec4 lightColour;
    // rgb: the ambient light's colour times its intensity, linear. a unused.
    vec4 ambient;
    // x: seconds since the app started; y: this frame's delta in seconds. zw unused.
    vec4 time;
} frame;

#endif
