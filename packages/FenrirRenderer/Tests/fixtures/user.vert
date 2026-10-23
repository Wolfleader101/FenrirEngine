#version 460
#extension GL_GOOGLE_include_directive : require
// A user's own vertex stage on the engine includes (ADR-0024): the draw's world matrix by base instance, the camera's
// matrices, and UV0 passed on.
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"
#include "Fenrir/View.glsl"

layout(location = 0) out vec2 vUv;

void main()
{
    vUv = inUv0;
    gl_Position = view.viewProjection * (CurrentDraw().world * vec4(inPosition, 1.0));
}
