#version 460
#extension GL_GOOGLE_include_directive : require
// The vertex stage of the built-in mesh programs (builtin:unlit, builtin:lit and the fallback): the item's world
// matrix from set 3 by base instance, the camera's from set 1, and the standard vertex streams.
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"
#include "Fenrir/View.glsl"

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vColour;

void main()
{
    DrawItem item = CurrentDraw();
    vec4 world = item.world * vec4(inPosition, 1.0);
    vWorldPosition = world.xyz;
    vNormal = mat3(item.normal) * inNormal;
    vColour = inColour;
    gl_Position = view.viewProjection * world;
}
