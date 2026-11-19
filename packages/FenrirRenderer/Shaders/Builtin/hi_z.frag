#version 460
// builtin:hi-z (ADR-0056, ADR-0057): one level of a view's Hi-Z pyramid, RG32F with a full mip chain: red the nearest
// depth of each texel's footprint, which the reflection trace marches through, green the farthest, which the cull
// pass's second phase tests candidates against. The COPY variant, the first HiZ pass, copies SceneDepth into level 0
// texel for texel, both channels the pixel's depth; with the MSAA key SceneDepth is multisampled and the nearest and
// the farthest of the pixel's samples are taken, so a pixel an edge cuts holds both sides. The base variant, each
// later HiZ pass, draws a level from the one above it, which the pass binds as its only level: each texel takes the
// largest nearest depth and the smallest farthest depth of the source texels it covers (reversed-Z, larger nearer),
// so no gap in the pyramid hides geometry the full-resolution depth has, and nothing drawn is nearer than the farthest.
// A level's size is the one above's halved and rounded down (GL's mip sizes), so where the source's side is odd the
// last texel of the level takes the source's last three texels on that side, not two, and no source texel is left out.

#if defined(COPY) && defined(MSAA)
layout(set = 2, binding = 0) uniform sampler2DMS source;
#else
layout(set = 2, binding = 0) uniform sampler2D source;
#endif

layout(set = 1, binding = 0, std140) uniform HiZData
{
    // xy: the source level's size in texels; zw: the level drawn's.
    uvec4 size;
} hiZ;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outDepth;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
#if defined(COPY) && defined(MSAA)
    int samples = textureSamples(source);
    float nearest = 0.0;
    float farthest = 1.0;
    for (int i = 0; i < samples; ++i)
    {
        float depth = texelFetch(source, pixel, i).r;
        nearest = max(nearest, depth);
        farthest = min(farthest, depth);
    }
    outDepth = vec4(nearest, farthest, 0.0, 1.0);
#elif defined(COPY)
    float depth = texelFetch(source, pixel, 0).r;
    outDepth = vec4(depth, depth, 0.0, 1.0);
#else
    ivec2 sourceSize = ivec2(hiZ.size.xy);
    ivec2 targetSize = ivec2(hiZ.size.zw);
    ivec2 last = sourceSize - 1;
    // Two source texels a side, three for the last texel on a side whose source is odd.
    ivec2 extent = ivec2(2) + ivec2(equal(pixel, targetSize - 1)) * (sourceSize & 1);
    float nearest = 0.0;
    float farthest = 1.0;
    for (int y = 0; y < extent.y; ++y)
    {
        for (int x = 0; x < extent.x; ++x)
        {
            vec2 depths = texelFetch(source, min(2 * pixel + ivec2(x, y), last), 0).rg;
            nearest = max(nearest, depths.r);
            farthest = min(farthest, depths.g);
        }
    }
    outDepth = vec4(nearest, farthest, 0.0, 1.0);
#endif
}
