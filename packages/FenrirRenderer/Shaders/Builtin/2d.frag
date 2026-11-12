#version 460
// builtin:2d, the fragment stage (ADR-0027): the vertex colour times the texture, both straight alpha. An sRGB image
// decodes on sampling, so the texel is linear. PREMULTIPLY (PremultipliedAlpha) multiplies RGB by alpha after any
// decode; GAMMA encodes the texel again, so on the gamma path every value is sRGB-encoded and passes through.
//
// BLUR and SHADOW are the 2D render feature's own, for offscreen groups' filters (ADR-0060), drawn with no other key
// over a premultiplied image: BLUR is one pass of a separable Gaussian along `blur.xy`, the vertex colour scaling the
// result; with SHADOW too, the blurred alpha is tinted by `shadow`, a premultiplied linear colour.

// Set 1, the command's: no parameter block (but under BLUR), then its one texture, white when the command binds none.
#ifdef BLUR
layout(set = 1, binding = 0, std140) uniform MaterialParams
{
    // The step between taps in UV, the step in standard deviations, and the taps on each side of the centre.
    vec4 blur;
    // The UV rectangle the image holds (min, max); taps outside it read transparent.
    vec4 bounds;
    // x: 1 to divide by the weights of the taps inside `bounds` only, as an edge clamps, 0 to divide by them all.
    vec4 options;
    // SHADOW's tint.
    vec4 shadow;
} params;
#endif
layout(set = 1, binding = 1) uniform sampler2D image;
#pragma fenrir default(image, white)

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColour;
layout(location = 0) out vec4 outColour;

vec3 Encode(vec3 linear)
{
    return mix(linear * 12.92, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055, greaterThan(linear, vec3(0.0031308)));
}

#ifdef BLUR
vec4 Blurred()
{
    int taps = int(params.blur.w);
    vec4 sum = vec4(0.0);
    float inside = 0.0;
    float total = 0.0;
    for (int i = -taps; i <= taps; ++i)
    {
        float along = float(i) * params.blur.z;
        float weight = exp(-0.5 * along * along);
        vec2 uv = vUv + params.blur.xy * float(i);
        total += weight;
        if (all(greaterThanEqual(uv, params.bounds.xy)) && all(lessThanEqual(uv, params.bounds.zw)))
        {
            sum += texture(image, uv) * weight;
            inside += weight;
        }
    }
    return sum / max(params.options.x > 0.5 ? inside : total, 1e-6);
}
#endif

void main()
{
#ifdef BLUR
#ifdef SHADOW
    outColour = vColour * params.shadow * Blurred().a;
#else
    outColour = vColour * Blurred();
#endif
#else
    vec4 texel = texture(image, vUv);
#ifdef GAMMA
    texel.rgb = Encode(texel.rgb);
#endif
    vec4 colour = vColour * texel;
#ifdef PREMULTIPLY
    colour.rgb *= colour.a;
#endif
    outColour = colour;
#endif
}
