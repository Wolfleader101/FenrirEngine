#version 460
#extension GL_GOOGLE_include_directive : require
// RendererDemo's stripes shader, a user's own material shader on the engine includes (ADR-0024): the vertex stage
// passes the object-space position on, so the stripes run across the mesh and move with it.
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"
#include "Fenrir/View.glsl"

layout(location = 0) out vec3 vLocal;
layout(location = 1) out vec3 vNormal;

void main()
{
    DrawItem item = CurrentDraw();
    vLocal = inPosition;
    vNormal = mat3(item.normal) * inNormal;
    gl_Position = view.viewProjection * (item.world * vec4(inPosition, 1.0));
}
