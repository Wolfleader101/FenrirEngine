#version 460
#extension GL_GOOGLE_include_directive : require
// RendererDemo's monitor screen, a user shader on the engine includes (ADR-0024): the mesh's first UV set passed on, so
// the fragment stage can show an image across the screen.
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"
#include "Fenrir/View.glsl"

layout(location = 0) out vec2 vUv;

void main()
{
    DrawItem item = CurrentDraw();
    vUv = inUv0;
    gl_Position = view.viewProjection * (item.world * vec4(inPosition, 1.0));
}
