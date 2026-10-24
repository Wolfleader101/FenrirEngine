#version 460
// builtin:2d, the fragment stage (ADR-0027): the vertex colour times the texture, both straight alpha. An sRGB image
// decodes on sampling, so the texel is linear. PREMULTIPLY (PremultipliedAlpha) multiplies RGB by alpha after any
// decode; GAMMA encodes the texel again, so on the gamma path every value is sRGB-encoded and passes through.

// Set 1, the command's: no parameter block, then its one texture, white when the command binds none.
layout(set = 1, binding = 1) uniform sampler2D image;
#pragma fenrir default(image, white)

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColour;
layout(location = 0) out vec4 outColour;

vec3 Encode(vec3 linear)
{
    return mix(linear * 12.92, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055, greaterThan(linear, vec3(0.0031308)));
}

void main()
{
    vec4 texel = texture(image, vUv);
#ifdef GAMMA
    texel.rgb = Encode(texel.rgb);
#endif
    vec4 colour = vColour * texel;
#ifdef PREMULTIPLY
    colour.rgb *= colour.a;
#endif
    outColour = colour;
}
