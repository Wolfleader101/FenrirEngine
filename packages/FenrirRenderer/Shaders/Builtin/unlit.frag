#version 460
// builtin:unlit (ADR-0024): base colour times base-colour texture times vertex colour, written as linear HDR. Its
// ALPHA_MASK variant, drawn for a Masked material, discards a pixel whose alpha is below the material's alpha cutoff,
// which the renderer writes into `alphaCutoff`.

#pragma fenrir default(baseColour, 1.0, 1.0, 1.0, 1.0)
#ifdef ALPHA_MASK
#pragma fenrir default(alphaCutoff, 0.5)
#endif
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA.
    vec4 baseColour;
#ifdef ALPHA_MASK
    // MaterialData::alphaCutoff, written by the renderer over any override.
    float alphaCutoff;
#endif
} material;
#pragma fenrir default(baseColourTexture, white)
layout(set = 2, binding = 1) uniform sampler2D baseColourTexture;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColour;
layout(location = 3) in vec2 vUv;
layout(location = 0) out vec4 outColour;

void main()
{
    vec4 colour = material.baseColour * texture(baseColourTexture, vUv) * vColour;
#ifdef ALPHA_MASK
    if (colour.a < material.alphaCutoff)
    {
        discard;
    }
#endif
    outColour = colour;
}
