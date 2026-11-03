#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:depth-prepass (ADR-0049): the fragment stage the DepthPrepass pass runs behind each opaque or masked item's
// own vertex stage. Depth is all it lays down for Opaque to test for equality; its one colour output is the surface's
// view-space normal, octahedral (Fenrir/Normals.glsl), into the view's RG16F normal target, facing the triangle's side
// that is seen as builtin:lit's normal does. It reads what mesh.vert writes: the world normal at location 1 and, under
// ALPHA_MASK, the vertex colour at 2 and UV0 at 3.
//
// Its ALPHA_MASK variant, drawn for a Masked material, discards a pixel as builtin:lit and builtin:unlit do: when the
// base colour's alpha times baseColourTexture's times the vertex colour's is below the alpha cutoff. Set 2 is the
// renderer's, not the material's: the base colour's alpha and the cutoff, and the material's base colour texture.
#include "Fenrir/Normals.glsl"
#include "Fenrir/View.glsl"

#ifdef ALPHA_MASK
layout(set = 2, binding = 0, std140) uniform PrepassMask
{
    // x: the material's base colour alpha; y: its alpha cutoff.
    vec4 alpha;
} mask;
#pragma fenrir default(baseColourTexture, white)
layout(set = 2, binding = 1) uniform sampler2D baseColourTexture;

layout(location = 2) in vec4 vColour;
layout(location = 3) in vec2 vUv;
#endif
layout(location = 1) in vec3 vNormal;
layout(location = 0) out vec2 outNormal;

void main()
{
#ifdef ALPHA_MASK
    if (mask.alpha.x * texture(baseColourTexture, vUv).a * vColour.a < mask.alpha.y)
    {
        discard;
    }
#endif
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing)
    {
        n = -n;
    }
    outNormal = FenrirOctEncode(normalize(mat3(view.view) * n));
}
