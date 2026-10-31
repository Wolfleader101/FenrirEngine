#version 460
// The Output pass (ADR-0019, ADR-0049): the view's linear HDR SceneColor scaled by its camera's exposure and mapped by
// its tonemapper into [0, 1], drawn into the view's viewport of its render target. The window's backbuffer and image
// targets are sRGB, so the write encodes it. With the MSAA key, SceneColor is multisampled and this resolves it: each
// pixel is the mean of its samples, before the tonemap. The tonemappers are mirrored step for step in C++
// (src/Core/Tonemapping.cpp), which the CPU tests check against published values and the GL tests hold this to; a
// change to one is a change to both. Matrices are written column by column.

#ifdef MSAA
layout(set = 2, binding = 0) uniform sampler2DMS sceneColour;
#else
layout(set = 2, binding = 0) uniform sampler2D sceneColour;
#endif

layout(set = 1, binding = 0, std140) uniform OutputData
{
    // 2^exposure, in EV stops.
    float exposureScale;
    // Render::Tonemapper: 0 None, 1 AgX, 2 PbrNeutral, 3 Aces.
    uint tonemapper;
} settings;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

// AgX as three.js r170 writes it, after Filament and Blender: into Rec. 2020, the inset matrix, a log2 encoding from
// 10 stops under to 6.5 stops over mid grey, the sigmoid's polynomial fit, the outset matrix, the fit's 2.2 gamma
// undone, and back to Rec. 709 (sRGB's primaries).
vec3 AgX(vec3 colour)
{
    const mat3 srgbToRec2020 =
        mat3(vec3(0.6274, 0.0691, 0.0164), vec3(0.3293, 0.9195, 0.0880), vec3(0.0433, 0.0113, 0.8956));
    const mat3 rec2020ToSrgb =
        mat3(vec3(1.6605, -0.1246, -0.0182), vec3(-0.5876, 1.1329, -0.1006), vec3(-0.0728, -0.0083, 1.1187));
    const mat3 inset = mat3(vec3(0.856627153315983, 0.137318972929847, 0.11189821299995),
                            vec3(0.0951212405381588, 0.761241990602591, 0.0767994186031903),
                            vec3(0.0482516061458583, 0.101439036467562, 0.811302368396859));
    const mat3 outset = mat3(vec3(1.1271005818144368, -0.1413297634984383, -0.14132976349843826),
                             vec3(-0.11060664309660323, 1.157823702216272, -0.11060664309660294),
                             vec3(-0.016493938717834573, -0.016493938717834257, 1.2519364065950405));
    // log2(0.18 x 2^-10) and log2(0.18 x 2^6.5).
    const float minEv = -12.47393;
    const float maxEv = 4.026069;

    vec3 x = inset * (srgbToRec2020 * colour);
    x = log2(max(x, vec3(1e-10)));
    x = clamp((x - minEv) / (maxEv - minEv), 0.0, 1.0);
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    x = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
    x = outset * x;
    x = pow(max(x, vec3(0.0)), vec3(2.2));
    return clamp(rec2020ToSrgb * x, 0.0, 1.0);
}

// Khronos PBR Neutral (https://github.com/KhronosGroup/ToneMapping): the darkest channel's small offset taken off, and
// above 0.76 the peak compressed towards 1 and the colour desaturated towards it.
vec3 PbrNeutral(vec3 colour)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(colour.r, min(colour.g, colour.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    colour -= offset;
    float peak = max(colour.r, max(colour.g, colour.b));
    if (peak < startCompression)
    {
        return colour;
    }
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    colour *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(colour, vec3(newPeak), g);
}

// Stephen Hill's fit (BakingLab's ACES.hlsl): sRGB into ACES's AP1 with the RRT's saturation, a rational fit of the RRT
// and ODT curves, and back to sRGB.
vec3 Aces(vec3 colour)
{
    const mat3 inputMatrix =
        mat3(vec3(0.59719, 0.07600, 0.02840), vec3(0.35458, 0.90834, 0.13383), vec3(0.04823, 0.01566, 0.83777));
    const mat3 outputMatrix =
        mat3(vec3(1.60475, -0.10208, -0.00327), vec3(-0.53108, 1.10813, -0.07276), vec3(-0.07367, -0.00605, 1.07602));
    vec3 v = inputMatrix * colour;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(outputMatrix * (a / b), 0.0, 1.0);
}

vec3 Tonemap(vec3 colour)
{
    switch (settings.tonemapper)
    {
    case 1u:
        return AgX(colour);
    case 2u:
        return PbrNeutral(colour);
    case 3u:
        return Aces(colour);
    default:
        return clamp(colour, 0.0, 1.0);
    }
}

void main()
{
    // NDC +y is up and a texture's row 0 is its top row, on every backend (ADR-0016).
    vec2 uv = vec2(vNdc.x * 0.5 + 0.5, 0.5 - vNdc.y * 0.5);
#ifdef MSAA
    ivec2 size = textureSize(sceneColour);
    ivec2 texel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    int samples = textureSamples(sceneColour);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < samples; ++i)
    {
        sum += texelFetch(sceneColour, texel, i).rgb;
    }
    vec3 colour = sum / float(samples);
#else
    vec3 colour = texture(sceneColour, uv).rgb;
#endif
    outColour = vec4(Tonemap(colour * settings.exposureScale), 1.0);
}
