#version 460
// A FlareGhosts pass (ADR-0059), after John Chapman's pseudo lens flare: from a bloom level, each pixel reads
// `ghostCount` ghosts along the line from it through the view's centre, ghost i at i * `ghostSpacing` of the way to the
// centre (past it once that passes 1), and a halo: the pixel `haloRadius` nearer the centre, where the pixel lies
// within `haloWidth` of the ring at that radius. Distances are taken in the view's own proportions, as a share of the
// centre-to-corner distance, so the halo is a circle. Each read takes red `chromaticOffset` further along the pixel's
// outward direction and blue as far back, so the ghosts fringe along the line through the centre.
//
// Each ghost's weight fades with its read point's distance from the centre, (1 - d)^2, and with how far it is
// magnified, (1 - i spacing)^2 held to 1, as a magnified ghost spreads its light thinner; nothing is read from outside
// the view. The halo's weight is (1 - its distance from the ring over its width)^2. When the weights sum past 1 they are
// scaled to sum to 1, and the pixel keeps the rest of its own light, so a flat view is left as it is: the flare is the
// view's light read from elsewhere, and its composite moves light, never adds it. The target is the level's size;
// FlareBlur blurs it and FlareComposite mixes it into SceneColor.

layout(set = 2, binding = 0) uniform sampler2D source;

layout(set = 1, binding = 0, std140) uniform LensFlareData
{
    // How much further along the line to the centre each ghost is read than the last.
    float ghostSpacing;
    // The halo ring's radius and half-width, 1 the centre-to-corner distance.
    float haloRadius;
    float haloWidth;
    // How far red and blue are read apart, in the same distance.
    float chromaticOffset;
    // How many ghosts each pixel reads.
    uint ghostCount;
    // The view's width over its height.
    float aspect;
} flare;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

// Lens space: the view's centre at 0, x scaled by the aspect so a distance is the same either way, and the corners at
// distance 1.
vec2 Scale()
{
    vec2 halfSize = vec2(flare.aspect, 1.0) * 0.5;
    return halfSize / length(halfSize);
}

vec2 ToLens(vec2 uv)
{
    return (uv - 0.5) * 2.0 * Scale();
}

vec2 ToUv(vec2 lens)
{
    return lens / (2.0 * Scale()) + 0.5;
}

bool Inside(vec2 uv)
{
    return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
}

// The source at the lens point `at`, red `chromaticOffset` further along `outwards` and blue as far back.
vec3 Fringed(vec2 at, vec2 outwards)
{
    vec2 offset = outwards * flare.chromaticOffset;
    return vec3(texture(source, ToUv(at + offset)).r, texture(source, ToUv(at)).g, texture(source, ToUv(at - offset)).b);
}

void main()
{
    // NDC +y is up and a texture's row 0 is its top row, on every backend (ADR-0016).
    vec2 uv = vec2(vNdc.x * 0.5 + 0.5, 0.5 - vNdc.y * 0.5);
    vec2 lens = ToLens(uv);
    float radius = length(lens);
    vec2 outwards = radius > 0.0 ? lens / radius : vec2(0.0);
    vec3 flared = vec3(0.0);
    float taken = 0.0;
    for (uint i = 1u; i <= flare.ghostCount; ++i)
    {
        float scale = 1.0 - float(i) * flare.ghostSpacing;
        vec2 at = lens * scale;
        if (!Inside(ToUv(at)))
        {
            continue;
        }
        float fade = 1.0 - min(length(at), 1.0);
        float weight = fade * fade * min(scale * scale, 1.0);
        flared += weight * Fringed(at, outwards);
        taken += weight;
    }

    if (flare.haloWidth > 0.0)
    {
        vec2 at = lens - outwards * flare.haloRadius;
        float ring = max(1.0 - abs(radius - flare.haloRadius) / flare.haloWidth, 0.0);
        if (ring > 0.0 && Inside(ToUv(at)))
        {
            float weight = ring * ring;
            flared += weight * Fringed(at, outwards);
            taken += weight;
        }
    }

    if (taken > 1.0)
    {
        flared /= taken;
        taken = 1.0;
    }
    flared += (1.0 - taken) * texture(source, uv).rgb;
    outColour = vec4(flared, 1.0);
}
