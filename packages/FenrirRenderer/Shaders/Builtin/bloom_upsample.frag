#version 460
// A BloomUpsample or BloomComposite pass (ADR-0049): the bloom level below, at half the target's size, filtered up with
// a 3 x 3 tent (1 2 1 / 2 4 2 / 1 2 1, over 16) one source texel apart, as CoD: Advanced Warfare's does. The weights sum
// to 1, so the energy is kept. The pipeline blends the result over its target by straight alpha: `blend` is the
// radius into a level, so a level holds (1 - radius) of itself and radius of every wider one, and the intensity into
// SceneColor.
// With the HORIZONTAL key it is a StreakUpsample or StreakComposite pass (ADR-0059): the streak level below, at half
// the target's width, filtered up with a horizontal tent (1 2 1, over 4) one source texel apart, and multiplied by
// `tint`: the streaks' tint at unit luminance into SceneColor, and white into a level and in every bloom pass.
// With the DIRT key it is a BloomComposite or FlareComposite pass under lens dirt (ADR-0059): its share of SceneColor,
// the alpha, is multiplied by 1 + `dirtIntensity` times the dirt image's luminance at the pixel and held at or below 1,
// so the mix stays a blend (LensDirtShare is its CPU mirror).

layout(set = 2, binding = 0) uniform sampler2D source;
#ifdef DIRT
// The lens dirt, stretched over the view.
layout(set = 2, binding = 1) uniform sampler2D dirt;
#endif

layout(set = 1, binding = 0, std140) uniform BloomData
{
    // One over the source's size: a texel, in UV.
    vec2 sourceTexel;
    // The share of the target the result takes.
    float blend;
    // The downsample's Karis flag (unused here).
    uint karis;
    // What the result is multiplied by.
    vec3 tint;
    // How much more of the result a pixel takes where the dirt is white, with the DIRT key.
    float dirtIntensity;
} bloom;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

vec3 Tap(vec2 uv, float x, float y)
{
    return texture(source, uv + vec2(x, y) * bloom.sourceTexel).rgb;
}

void main()
{
    // NDC +y is up and a texture's row 0 is its top row, on every backend (ADR-0016).
    vec2 uv = vec2(vNdc.x * 0.5 + 0.5, 0.5 - vNdc.y * 0.5);
#ifdef HORIZONTAL
    vec3 filtered = 0.25 * (2.0 * Tap(uv, 0.0, 0.0) + Tap(uv, -1.0, 0.0) + Tap(uv, 1.0, 0.0));
#else
    vec3 sum = 4.0 * Tap(uv, 0.0, 0.0);
    sum += 2.0 * (Tap(uv, -1.0, 0.0) + Tap(uv, 1.0, 0.0) + Tap(uv, 0.0, -1.0) + Tap(uv, 0.0, 1.0));
    sum += Tap(uv, -1.0, -1.0) + Tap(uv, 1.0, -1.0) + Tap(uv, -1.0, 1.0) + Tap(uv, 1.0, 1.0);
    vec3 filtered = sum / 16.0;
#endif
    float share = bloom.blend;
#ifdef DIRT
    float lensDirt = max(dot(texture(dirt, uv).rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0);
    share = min(share * (1.0 + max(bloom.dirtIntensity, 0.0) * lensDirt), 1.0);
#endif
    outColour = vec4(filtered * bloom.tint, share);
}
