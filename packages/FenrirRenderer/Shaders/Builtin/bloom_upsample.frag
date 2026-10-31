#version 460
// A BloomUpsample or BloomComposite pass (ADR-0049): the bloom level below, at half the target's size, filtered up with
// a 3 x 3 tent (1 2 1 / 2 4 2 / 1 2 1, over 16) one source texel apart, as CoD: Advanced Warfare's does. The weights sum
// to 1, so the energy is kept. The pipeline blends the result over its target by straight alpha: `blend` is the
// radius into a level, so a level holds (1 - radius) of itself and radius of every wider one, and the intensity into
// SceneColor.

layout(set = 2, binding = 0) uniform sampler2D source;

layout(set = 1, binding = 0, std140) uniform BloomData
{
    // One over the source's size: a texel, in UV.
    vec2 sourceTexel;
    // The share of the target the result takes.
    float blend;
    // The downsample's Karis flag (unused here).
    uint karis;
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
    vec3 sum = 4.0 * Tap(uv, 0.0, 0.0);
    sum += 2.0 * (Tap(uv, -1.0, 0.0) + Tap(uv, 1.0, 0.0) + Tap(uv, 0.0, -1.0) + Tap(uv, 0.0, 1.0));
    sum += Tap(uv, -1.0, -1.0) + Tap(uv, 1.0, -1.0) + Tap(uv, -1.0, 1.0) + Tap(uv, 1.0, 1.0);
    outColour = vec4(sum / 16.0, bloom.blend);
}
