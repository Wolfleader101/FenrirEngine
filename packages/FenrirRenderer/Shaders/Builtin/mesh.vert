#version 460
#extension GL_GOOGLE_include_directive : require
// The vertex stage of the built-in mesh programs (builtin:unlit, builtin:lit and the fallback): the item's world
// matrix from set 3 by base instance, the camera's from set 1, and the standard vertex streams. Under SKINNED
// (ADR-0035) the vertex is skinned from the item's palette range first, through Fenrir/Skinning.glsl.
//
// The tangent (ADR-0048) goes to world space as positions' directions do, through the world 3x3 (the normal matrix is
// for normals), with its handedness times the sign of the world matrix's determinant, so a mirroring draw keeps its
// bitangent. A mesh without tangents reads the zero tangent, and its handedness 0 tells the fragment stage so.
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
// xyz: the world tangent, unnormalised; w: the bitangent's sign in world space, or 0 for no tangent.
layout(location = 4) out vec4 vTangent;

void main()
{
    DrawItem item = CurrentDraw();
#ifdef SKINNED
    vec4 world = vec4(FenrirSkinPosition(item, inPosition), 1.0);
    vNormal = FenrirSkinNormal(item, inNormal);
    vTangent = FenrirSkinTangent(item, inTangent, vNormal);
#else
    vec4 world = item.world * vec4(inPosition, 1.0);
    vNormal = mat3(item.normal) * inNormal;
    vTangent = vec4(mat3(item.world) * inTangent.xyz, inTangent.w * item.worldSign);
#endif
    vWorldPosition = world.xyz;
    vColour = inColour;
    vUv = inUv0;
    gl_Position = view.viewProjection * world;
}
