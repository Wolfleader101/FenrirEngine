#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:shadow (ADR-0050): the fragment stage the shadow passes run behind each casting item's own vertex stage, whose
// view block holds the cascade's light matrix. Depth is all they lay down, into the shadow atlas; there is no colour.
//
// Its ALPHA_MASK variant, drawn for a Masked material, discards a pixel as builtin:depth-prepass's does, from the same
// set 2 (the base colour's alpha and the cutoff, and the material's base colour texture): when the base colour's alpha
// times baseColourTexture's times the vertex colour's is below the alpha cutoff. It reads what mesh.vert writes: the
// vertex colour at location 2 and UV0 at 3.

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
}
