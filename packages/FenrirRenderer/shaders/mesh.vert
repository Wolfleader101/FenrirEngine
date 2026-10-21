#version 460
#extension GL_GOOGLE_include_directive : require
#include "Fenrir/View.glsl"
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec4 vColour;

void main()
{
    DrawItem item = CurrentDraw();
    vec4 world = item.world * vec4(inPosition, 1.0);
    vWorldPosition = world.xyz;
    vNormal = mat3(item.normal) * inNormal;
    vUv = inUv;
    vColour = inColour;
    gl_Position = view.viewProjection * world;
}
