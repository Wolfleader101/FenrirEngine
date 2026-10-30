#version 460
#extension GL_GOOGLE_include_directive : require
// The vertex stage of the built-in mesh programs (builtin:unlit, builtin:lit and the fallback): the item's world
// matrix from set 3 by base instance, the camera's from set 1, and the standard vertex streams. Under SKINNED
// (ADR-0035) the vertex is skinned from the item's palette range first, through Fenrir/Skinning.glsl.
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"
#include "Fenrir/View.glsl"
#ifdef SKINNED
#include "Fenrir/Skinning.glsl"
#endif

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vColour;
layout(location = 3) out vec2 vUv;

void main()
{
    DrawItem item = CurrentDraw();
#ifdef SKINNED
    vec4 world = vec4(FenrirSkinPosition(item, inPosition), 1.0);
    vNormal = FenrirSkinNormal(item, inNormal);
#else
    vec4 world = item.world * vec4(inPosition, 1.0);
    vNormal = mat3(item.normal) * inNormal;
#endif
    vWorldPosition = world.xyz;
    vColour = inColour;
    vUv = inUv0;
    gl_Position = view.viewProjection * world;
}
