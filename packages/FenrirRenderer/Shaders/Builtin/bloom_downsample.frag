#version 460
// A BloomDownsample pass (ADR-0049): its source, SceneColor or the bloom level above, filtered into a level at half its
// size with Jimenez's 13-tap filter (CoD: Advanced Warfare, SIGGRAPH 2014). Each output pixel sits on a corner of its
// source's texels; thirteen bilinear taps there and one and two texels out make five overlapping 2 x 2 boxes, the
// middle one weighted 1/2 and the four corner ones 1/8 each, so the weights sum to 1 and the level keeps the energy.
// The first pass takes the Karis average, each box weighted by 1 / (1 + its luma), so a lone very bright pixel cannot
// make the bloom flicker. The source is mirrored at its edges, which keeps its energy. With the MSAA key it is a
// multisampled SceneColor: the 6 x 6 texels under the taps are each the mean of their samples, read with texelFetch and
// mirrored at the edges as the sampler mirrors, and the taps averaged from them as bilinear taps would be.

#ifdef MSAA
layout(set = 2, binding = 0) uniform sampler2DMS source;
#else
layout(set = 2, binding = 0) uniform sampler2D source;
#endif

layout(set = 1, binding = 0, std140) uniform BloomData
{
    // One over the source's size: a texel, in UV.
    vec2 sourceTexel;
    // The upsample's blend (unused here).
    float blend;
    // 1 for the Karis average.
    uint karis;
} bloom;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

float Luma(vec3 colour)
{
    return dot(colour, vec3(0.2126, 0.7152, 0.0722));
}

#ifdef MSAA
// The texel a mirrored-repeat sampler reads for `texel`: one step past an edge is the edge texel again, and so on. The
// taps reach at most three texels past an edge, so adding four periods first keeps % away from negative operands.
ivec2 Mirror(ivec2 texel, ivec2 size)
{
    ivec2 period = 2 * size;
    ivec2 at = (texel + 4 * period) % period;
    return min(at, period - 1 - at);
}

// The resolved texels from three left of and above the centre corner to two right of and below it.
vec3 texels[6][6];

void ReadTexels(vec2 uv)
{
    ivec2 size = textureSize(source);
    int samples = textureSamples(source);
    ivec2 corner = ivec2(round(uv * vec2(size)));
    for (int y = 0; y < 6; ++y)
    {
        for (int x = 0; x < 6; ++x)
        {
            ivec2 texel = Mirror(corner + ivec2(x - 3, y - 3), size);
            vec3 sum = vec3(0.0);
            for (int i = 0; i < samples; ++i)
            {
                sum += texelFetch(source, texel, i).rgb;
            }
            texels[y][x] = sum / float(samples);
        }
    }
}

// The bilinear tap on the corner `x`, `y` texels from the centre one: the mean of the four texels round it.
vec3 Tap(int x, int y)
{
    return 0.25 * (texels[y + 2][x + 2] + texels[y + 2][x + 3] + texels[y + 3][x + 2] + texels[y + 3][x + 3]);
}
#else
vec2 centre;

vec3 Tap(int x, int y)
{
    return texture(source, centre + vec2(x, y) * bloom.sourceTexel).rgb;
}
#endif

void main()
{
    // NDC +y is up and a texture's row 0 is its top row, on every backend (ADR-0016).
    vec2 uv = vec2(vNdc.x * 0.5 + 0.5, 0.5 - vNdc.y * 0.5);
#ifdef MSAA
    ReadTexels(uv);
#else
    centre = uv;
#endif
    vec3 a = Tap(-2, -2), b = Tap(0, -2), c = Tap(2, -2);
    vec3 d = Tap(-2, 0), e = Tap(0, 0), f = Tap(2, 0);
    vec3 g = Tap(-2, 2), h = Tap(0, 2), i = Tap(2, 2);
    vec3 j = Tap(-1, -1), k = Tap(1, -1), l = Tap(-1, 1), m = Tap(1, 1);

    vec3 boxes[5] = vec3[](0.25 * (j + k + l + m), 0.25 * (a + b + d + e), 0.25 * (b + c + e + f),
                           0.25 * (d + e + g + h), 0.25 * (e + f + h + i));
    const float weights[5] = float[](0.5, 0.125, 0.125, 0.125, 0.125);
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int box = 0; box < 5; ++box)
    {
        float weight = bloom.karis != 0u ? weights[box] / (1.0 + Luma(boxes[box])) : weights[box];
        sum += boxes[box] * weight;
        total += weight;
    }
    outColour = vec4(sum / total, 1.0);
}
