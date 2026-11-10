#version 460
#extension GL_GOOGLE_include_directive : require
// The Output pass (ADR-0019, ADR-0049, ADR-0059): the view's linear HDR SceneColor scaled by its camera's exposure,
// graded by its colour grade and mapped by its tonemapper into [0, 1], drawn into the view's viewport of its render
// target. The window's backbuffer and image targets are sRGB, so the write encodes it. With the MSAA key, SceneColor
// is multisampled and this resolves it: each pixel is the mean of its samples, before the tonemap. The tonemappers and
// the grade are mirrored step for step in C++ (src/Core/Tonemapping.cpp, src/Core/ColourGrading.cpp), which the CPU
// tests check against published values and the GL tests hold this to; a change to one is a change to both. Matrices
// are written column by column. With the OCCLUSION key (a camera's grey debug views), the texture is single-sampled,
// and its `greyChannel` is drawn as grey as it is: no exposure, no grade, no tonemap. With the REFLECTIONS key
// (CameraDebugView::Reflections, ADR-0056), it is the view's reflection hit target, and the colour is last frame's
// history at each hit's UV, its sharpest level, times the hit's confidence: what a mirror would blend, exposed, graded
// and tonemapped. The hit target is read texel by texel, as Fenrir/Lighting.glsl reads it, so neighbouring hits' UVs
// are never blended. REFLECTIONS overrides the other keys. With the ABERRATION key (a camera's chromatic aberration),
// each channel is read at its own point, red nearer the view's centre and blue further out, so red fringes outwards
// and blue inwards; under MSAA red and blue are read between texels as the single-sampled sampler reads them, the
// four texels round each resolved and blended bilinearly, nine resolves instead of one. The vignette, a
// multiply after the exposure, and the film grain, after the tonemap, follow every variant but OCCLUSION, and are
// mirrored in src/Core/CameraEffects.cpp as the grade is.
#include "Fenrir/Constants.glsl"

#if defined(REFLECTIONS)
// r, g: the hit's UV in last frame's view; b: its confidence. At the view's size.
layout(set = 2, binding = 0) uniform sampler2D reflectionHits;
layout(set = 2, binding = 1) uniform sampler2D sceneHistory;
#elif defined(MSAA) && !defined(OCCLUSION)
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
    // With OCCLUSION, the channel drawn as grey: 0 red, 2 blue.
    uint greyChannel;
    // Whether the colour is graded: 0 for an identity grade, which is skipped. The rest is Render::ColourGrade.
    uint grade;
    // Linear Rec. 709 to itself, from the white the camera names to D65.
    mat3 whiteBalance;
    // Each tint at unit luminance.
    vec3 shadowTint;
    float contrast;
    vec3 highlightTint;
    // The split between the tints, in stops from mid grey.
    float split;
    vec3 slope;
    float saturation;
    vec3 offset;
    vec3 power;
    // With ABERRATION, how far red and blue are read apart, as a fraction of the half-diagonal at the corners.
    float aberration;
    // The vignette's intensity (0 off), radius, smoothness and roundness.
    vec4 vignette;
    // The grain's intensity (0 off), response and size, in pixels.
    vec3 grain;
    // The frame's index, which the grain's noise is a hash of with each cell.
    uint frameIndex;
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

// The colour grade (ADR-0059), on the exposed linear colour: white balance, contrast in log2 around mid grey, split
// toning by luminance in log2, saturation around luminance, then the ASC CDL with negatives clamped before its power.
vec3 Grade(vec3 colour)
{
    if (settings.grade == 0u)
    {
        return colour;
    }
    const float midGrey = 0.18;
    // How many stops either side of the split the shadow tint crosses into the highlight tint over.
    const float splitWidth = 4.0;
    colour = settings.whiteBalance * colour;
    colour = midGrey * pow(max(colour / midGrey, vec3(1e-10)), vec3(settings.contrast));
    float stops = log2(max(dot(colour, FenrirLuminance), 1e-10) / midGrey);
    float highlight = smoothstep(settings.split - splitWidth, settings.split + splitWidth, stops);
    colour *= mix(settings.shadowTint, settings.highlightTint, highlight);
    colour = mix(vec3(dot(colour, FenrirLuminance)), colour, settings.saturation);
    return pow(max(colour * settings.slope + settings.offset, vec3(0.0)), settings.power);
}

// The vignette (ADR-0059): 1 - intensity x smoothstep(radius, radius + smoothness, d), where d is the distance from
// the centre, 1 at the corners, in halves of the view's height when round and in each axis' own halves when not.
vec3 Vignette(vec3 colour, vec2 uv, vec2 size)
{
    float intensity = settings.vignette.x;
    float radius = settings.vignette.y;
    float smoothness = settings.vignette.z;
    float roundness = settings.vignette.w;
    if (intensity <= 0.0)
    {
        return colour;
    }
    vec2 scale = vec2(1.0 + (size.x / size.y - 1.0) * roundness, 1.0);
    float distance = length((uv - 0.5) * scale) / length(0.5 * scale);
    float edge = radius + max(smoothness, 1e-4);
    return colour * (1.0 - intensity * smoothstep(radius, edge, distance));
}

// The grain's noise for one cell and frame: two uniforms from a PCG3D hash (Jarzynski and Olano 2020), summed into a
// triangle on [-1, 1).
float GrainNoise(uvec2 cell, uint frame)
{
    uvec3 v = uvec3(cell, frame) * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    return float((v.x >> 8u) + (v.y >> 8u)) * (1.0 / 16777216.0) - 1.0;
}

// Film grain (ADR-0059), on the tonemapped colour: added in a 2.2 gamma encoding, weighted towards the mid-tones of
// its encoded luminance by the response, the same in every channel.
vec3 Grain(vec3 colour, vec2 uv, vec2 size)
{
    float intensity = settings.grain.x;
    float response = settings.grain.y;
    float cellSize = settings.grain.z;
    if (intensity <= 0.0)
    {
        return colour;
    }
    uvec2 pixel = uvec2(floor(uv * size));
    uvec2 cell = uvec2(floor(vec2(pixel) / max(cellSize, 1.0)));
    float noise = GrainNoise(cell, settings.frameIndex);
    vec3 encoded = pow(max(colour, vec3(0.0)), vec3(1.0 / 2.2));
    float luminance = clamp(dot(encoded, FenrirLuminance), 0.0, 1.0);
    float weight = 1.0 + (4.0 * luminance * (1.0 - luminance) - 1.0) * response;
    return pow(clamp(encoded + intensity * weight * noise, 0.0, 1.0), vec3(2.2));
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

// The exposed colour through the vignette, the grade, the tonemapper and the grain, in ADR-0059's order.
vec3 Finish(vec3 colour, vec2 uv, vec2 size)
{
    colour = Vignette(colour * settings.exposureScale, uv, size);
    return Grain(Tonemap(Grade(colour)), uv, size);
}

#if !defined(REFLECTIONS) && !defined(OCCLUSION)
#ifdef MSAA
// The mean of the samples of SceneColor's `texel`, held to its edges: the resolve.
vec3 ResolveTexel(ivec2 texel, ivec2 size)
{
    texel = clamp(texel, ivec2(0), size - 1);
    int samples = textureSamples(sceneColour);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < samples; ++i)
    {
        sum += texelFetch(sceneColour, texel, i).rgb;
    }
    return sum / float(samples);
}

// The resolve of SceneColor's texel at `uv`.
vec3 Resolve(vec2 uv, ivec2 size)
{
    return ResolveTexel(ivec2(uv * vec2(size)), size);
}

#ifdef ABERRATION
// SceneColor resolved at `uv` between texels, as the Output sampler reads a single-sampled SceneColor: the four
// texels round it resolved and blended bilinearly, clamped at the edges.
vec3 ResolveBilinear(vec2 uv, ivec2 size)
{
    vec2 position = uv * vec2(size) - 0.5;
    ivec2 texel = ivec2(floor(position));
    vec2 weight = position - floor(position);
    vec3 top = mix(ResolveTexel(texel, size), ResolveTexel(texel + ivec2(1, 0), size), weight.x);
    vec3 bottom = mix(ResolveTexel(texel + ivec2(0, 1), size), ResolveTexel(texel + ivec2(1, 1), size), weight.x);
    return mix(top, bottom, weight.y);
}
#endif
#endif

// SceneColor at `uv`, resolved under MSAA.
vec3 Read(vec2 uv, ivec2 size)
{
#ifdef MSAA
    return Resolve(uv, size);
#else
    return texture(sceneColour, uv).rgb;
#endif
}

#ifdef ABERRATION
// SceneColor at `uv` between texels, as the sampler reads it, resolved under MSAA: where aberration reads red and blue.
vec3 ReadBetween(vec2 uv, ivec2 size)
{
#ifdef MSAA
    return ResolveBilinear(uv, size);
#else
    return texture(sceneColour, uv).rgb;
#endif
}
#endif
#endif

void main()
{
    // NDC +y is up and a texture's row 0 is its top row, on every backend (ADR-0016).
    vec2 uv = vec2(vNdc.x * 0.5 + 0.5, 0.5 - vNdc.y * 0.5);
#if defined(REFLECTIONS)
    ivec2 size = textureSize(reflectionHits, 0);
    vec4 hit = texelFetch(reflectionHits, clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1), 0);
    vec3 colour = hit.b > 0.0 ? textureLod(sceneHistory, hit.rg, 0.0).rgb * hit.b : vec3(0.0);
    outColour = vec4(Finish(colour, uv, vec2(size)), 1.0);
#elif defined(OCCLUSION)
    outColour = vec4(vec3(texture(sceneColour, uv)[settings.greyChannel]), 1.0);
#else
#ifdef MSAA
    ivec2 size = textureSize(sceneColour);
#else
    ivec2 size = textureSize(sceneColour, 0);
#endif
#ifdef ABERRATION
    // Red read nearer the centre shows further out; blue read further out shows nearer in.
    vec2 fromCentre = uv - 0.5;
    vec3 colour = vec3(ReadBetween(0.5 + fromCentre * (1.0 - settings.aberration), size).r, Read(uv, size).g,
                       ReadBetween(0.5 + fromCentre * (1.0 + settings.aberration), size).b);
#else
    vec3 colour = Read(uv, size);
#endif
    outColour = vec4(Finish(colour, uv, vec2(size)), 1.0);
#endif
}
