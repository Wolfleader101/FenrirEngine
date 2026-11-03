#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:shadow (ADR-0050): the fragment stage the shadow passes run behind each casting item's own vertex stage, whose
// view block holds the cascade's light matrix. Depth is all they lay down, into the shadow atlas; there is no colour.
//
// Its ALPHA_MASK variant, drawn for a Masked material, discards a pixel as builtin:depth-prepass's does, from the same
// set 2 (the base colour's alpha and the cutoff, and the material's base colour texture): when the base colour's alpha
// times baseColourTexture's times the vertex colour's is below the alpha cutoff. It reads what mesh.vert writes: the
// vertex colour at location 2 and UV0 at 3.
//
// Its POINT_SHADOW variant, drawn into a point light's cube faces, writes the distance from the light instead, over the
// light's range and reversed (1 at the light, 0 at its range and past it), so a receiver compares its own distance with
// it whichever face it reads. The distance comes from the fragment's place in its face and the depth the face's
// infinite reversed-Z perspective gave it (near over the distance along the face's axis), through the view block's
// projection and viewport; the view block's camera is the light, its range in w. No input of the vertex stage is
// needed, so any item's vertex stage serves, and it is the same whether the face was drawn alone or as an instance.
#ifdef POINT_SHADOW
#include "Fenrir/View.glsl"
#endif

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

void main()
{
#ifdef ALPHA_MASK
    if (mask.alpha.x * texture(baseColourTexture, vUv).a * vColour.a < mask.alpha.y)
    {
        discard;
    }
#endif
#ifdef POINT_SHADOW
    vec2 ndc = (gl_FragCoord.xy - view.viewport.xy) / view.viewport.zw * 2.0 - 1.0;
    float along = view.projection[3][2] / max(gl_FragCoord.z, 1e-30);
    float distance = along * length(vec3(ndc.x / view.projection[0][0], ndc.y / view.projection[1][1], 1.0));
    gl_FragDepth = 1.0 - distance / view.cameraPosition.w;
#endif
}
