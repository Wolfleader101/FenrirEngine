#version 460
// builtin:scene-history (ADR-0056): one level of a view's history colour, RGBA16F with a full mip chain, which the
// reflections sample last frame's scene from. The base variant, the first SceneHistory pass, copies SceneColor before
// bloom into level 0; with the MSAA key SceneColor is multisampled and each texel is the plain mean of its samples (not
// bloom's Karis average, which would darken the bright pixels reflections are for). As bloom's first level does, it
// clamps infinities to RGBA16F's largest values and leaves out NaN, so one bad pixel cannot spread through every level.
// The DOWNSAMPLE variant, each later SceneHistory pass, draws a level from the one above it, which the pass binds as
// its only level: each texel the mean of the 2 x 2 source texels it covers, clamped at the source's edges.

#if defined(MSAA) && !defined(DOWNSAMPLE)
layout(set = 2, binding = 0) uniform sampler2DMS source;
#else
layout(set = 2, binding = 0) uniform sampler2D source;
#endif

layout(set = 1, binding = 0, std140) uniform HistoryData
{
    // xy: the source's size in texels; zw: the level drawn's.
    uvec4 size;
} history;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
#ifdef DOWNSAMPLE
    ivec2 last = ivec2(history.size.xy) - 1;
    ivec2 first = 2 * pixel;
    outColour = 0.25 * (texelFetch(source, min(first, last), 0) + texelFetch(source, min(first + ivec2(1, 0), last), 0) +
                        texelFetch(source, min(first + ivec2(0, 1), last), 0) +
                        texelFetch(source, min(first + ivec2(1, 1), last), 0));
#else
#ifdef MSAA
    int samples = textureSamples(source);
    vec4 sum = vec4(0.0);
    for (int i = 0; i < samples; ++i)
    {
        sum += texelFetch(source, pixel, i);
    }
    vec4 colour = sum / float(samples);
#else
    vec4 colour = texelFetch(source, pixel, 0);
#endif
    outColour = any(isnan(colour)) ? vec4(0.0) : clamp(colour, vec4(-65504.0), vec4(65504.0));
#endif
}
