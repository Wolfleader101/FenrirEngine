// Fenrir/View.glsl: binding set 1 of every material shader, the view (ADR-0024): one camera drawing one viewport.
// Depth is reversed-Z in [0, 1] with an infinite far plane, and clip space has an upper-left origin (ADR-0016,
// ADR-0026). The renderer declares the same layout in C++ and checks it against a shader's reflection in debug builds.
#ifndef FENRIR_VIEW_GLSL
#define FENRIR_VIEW_GLSL

layout(set = 1, binding = 0, std140) uniform ViewData
{
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    // xyz: the camera's world position. w unused.
    vec4 cameraPosition;
    // The viewport in pixels of its target: x, y (top left), width, height.
    vec4 viewport;
} view;

// The view's ambient occlusion (ADR-0049) at its pixels, as its GTAO passes make it from the depth prepass: in red, how
// much of the sky reaches each pixel's nearest opaque surface, 0 to 1. The 1 x 1 white texture when the view's camera
// has ambient occlusion off, and in every pass but Opaque, so transparent items are not darkened by what is behind them.
layout(set = 1, binding = 1) uniform sampler2D fenrirAmbientOcclusion;

#endif
