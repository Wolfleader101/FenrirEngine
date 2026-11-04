#version 460
// builtin:hi-z (ADR-0056): one level of a view's hierarchical-Z pyramid, R32F with a full mip chain, which the
// reflection trace marches through. The COPY variant, the first HiZ pass, copies SceneDepth into level 0 texel for
// texel; with the MSAA key SceneDepth is multisampled and sample 0 is read, as builtin:gtao reads it. The base variant,
// each later HiZ pass, draws a level from the one above it, which the pass binds as its only level: each texel takes
// the largest of the source texels it covers, the nearest depth under reversed-Z, so no gap in the pyramid hides
// geometry the full-resolution depth has. A level's size is the one above's halved and rounded down (GL's mip sizes),
// so where the source's side is odd the last texel of the level takes the source's last three texels on that side,
// not two, and no source texel is left out.

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
#ifdef COPY
    outDepth = vec4(texelFetch(source, pixel, 0).r, 0.0, 0.0, 1.0);
#else
    ivec2 sourceSize = ivec2(hiZ.size.xy);
    ivec2 targetSize = ivec2(hiZ.size.zw);
    ivec2 last = sourceSize - 1;
    // Two source texels a side, three for the last texel on a side whose source is odd.
    ivec2 extent = ivec2(2) + ivec2(equal(pixel, targetSize - 1)) * (sourceSize & 1);
    float nearest = 0.0;
    for (int y = 0; y < extent.y; ++y)
    {
        for (int x = 0; x < extent.x; ++x)
        {
            nearest = max(nearest, texelFetch(source, min(2 * pixel + ivec2(x, y), last), 0).r);
        }
    }
    outDepth = vec4(nearest, 0.0, 0.0, 1.0);
#endif
}
