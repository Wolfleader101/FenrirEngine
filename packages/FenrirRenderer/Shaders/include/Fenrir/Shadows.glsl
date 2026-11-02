// Fenrir/Shadows.glsl: the sun's cascaded shadow and the spot and point lights' shadows (ADR-0050), from the view block's
// cascades and shadow atlas (Fenrir/View.glsl) and the frame's spot and point shadows (Fenrir/Frame.glsl).
// Fenrir/Lighting.glsl darkens the directional light and each shadowed spot and point light by them; a shader may call
// them itself.
//
// Each cascade is a texel-snapped orthographic view of the sun, so its texels stay on the world's grid as the camera
// moves. A pixel reads the first cascade whose depth range holds it, and in the last share of that range (the view
// block's shadowBias.w) blends towards the next, the last fading out, so no boundary shows. Each read is hardware
// depth comparison, bilinear, at 16 taps of a Vogel disk turned per pixel by interleaved gradient noise (Jimenez 2014),
// softer than a grid at the same cost, the noise breaking the banding a fixed pattern leaves. The pixel is moved before
// the read along its normal by the normal-offset bias and towards the sun by the slope-scaled bias, both in the
// cascade's own texels, so neither grows or shrinks with the cascades.
//
// A spot light's shadow is a perspective tile of the frame's spot shadow atlas, read the same way: the same filter, and
// the same biases in its own texels, whose world size grows with the distance along the light.
//
// A point light's shadow is a cube of distances from it, read along the way from the light to the pixel against the
// pixel's own distance: the same Vogel disk, laid across that way, and the same biases, in its texels' world size where
// the pixel is. The cube's faces filter into each other, so its edges show no seam.
#ifndef FENRIR_SHADOWS_GLSL
#define FENRIR_SHADOWS_GLSL

#include "Fenrir/Frame.glsl"
#include "Fenrir/View.glsl"

// Taps of the Vogel disk per cascade read.
const int FenrirShadowTaps = 16;
// The steepest slope the slope-scaled bias follows: past it, near grazing, the light falls off with N.L anyway.
const float FenrirShadowMaxSlope = 8.0;

// Jimenez's interleaved gradient noise at pixel `pixel`, in [0, 1).
float FenrirInterleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Tap `i` of `count` on the unit Vogel disk, turned by `angle`: the golden angle apart, at radii spreading the taps
// evenly over its area.
vec2 FenrirVogelDisk(int i, int count, float angle)
{
    const float goldenAngle = 2.39996323;
    float radius = sqrt((float(i) + 0.5) / float(count));
    float theta = float(i) * goldenAngle + angle;
    return radius * vec2(cos(theta), sin(theta));
}

// The shaded point moved by a light's biases before its shadow is read: `normalBias` texels of `texel` world size along
// the world normal `normal`, and `slopeBias` of them per unit of slope towards the light along `l`, for N.L `nDotL`.
vec3 FenrirShadowBiased(vec3 position, vec3 normal, vec3 l, float nDotL, float texel, float normalBias,
                        float slopeBias)
{
    float slope = min(sqrt(max(1.0 - nDotL * nDotL, 0.0)) / max(nDotL, 1e-4), FenrirShadowMaxSlope);
    return position + normal * (normalBias * texel) + l * (slopeBias * texel * max(slope, 0.1));
}

// The lit share of the Vogel-disk taps round `atlas.xy` in `shadowAtlas`, compared against depth `atlas.z`: `radius`
// texels of `texelUv` across, turned by `angle`, each read held between `low` and `high` (its tile, less the reach).
float FenrirFilteredShadow(sampler2DShadow shadowAtlas, vec3 atlas, vec2 low, vec2 high, vec2 texelUv, float radius,
                           float angle)
{
    float lit = 0.0;
    for (int i = 0; i < FenrirShadowTaps; ++i)
    {
        vec2 offset = FenrirVogelDisk(i, FenrirShadowTaps, angle) * radius * texelUv;
        // Level 0 explicitly: the caller may be in non-uniform control flow.
        lit += textureLod(shadowAtlas, vec3(clamp(atlas.xy + offset, low, high), atlas.z), 0.0);
    }
    return lit / float(FenrirShadowTaps);
}

// How much of the sun reaches `position` through cascade `cascade`, 0 to 1, for the world normal `normal` facing the
// sun at N.L `nDotL` along `l` (towards the sun), the disk turned by `angle`.
float FenrirCascadeShadow(int cascade, vec3 position, vec3 normal, vec3 l, float nDotL, float angle)
{
    vec3 biased = FenrirShadowBiased(position, normal, l, nDotL, view.shadowTexels[cascade], view.shadowBias.x,
                                     view.shadowBias.y);
    vec3 atlas = (view.shadowMatrices[cascade] * vec4(biased, 1.0)).xyz;

    // Read only inside the cascade's own tile of the atlas, the filter's reach in from its edges.
    vec2 size = vec2(textureSize(fenrirShadowAtlas, 0));
    float tiles = float(view.shadowInfo.x);
    vec2 texelUv = 1.0 / size;
    float reach = view.shadowBias.z + 1.0;
    vec2 low = vec2(float(cascade) / tiles, 0.0) + reach * texelUv;
    vec2 high = vec2(float(cascade + 1) / tiles, 1.0) - reach * texelUv;
    return FenrirFilteredShadow(fenrirShadowAtlas, atlas, low, high, texelUv, view.shadowBias.z, angle);
}

// The cascade a point `viewDepth` along the camera's axis reads, -1 past the last.
int FenrirShadowCascade(float viewDepth)
{
    int count = int(view.shadowInfo.x);
    for (int cascade = 0; cascade < count; ++cascade)
    {
        if (viewDepth < view.shadowSplits[cascade])
        {
            return cascade;
        }
    }
    return -1;
}

// How much of the sun reaches `position`, a world position on a face of world normal `normal` (the face's own, not a
// normal map's), 0 to 1: 1 where the view has no shadow, past its last cascade, and on a face turned from the sun,
// which the light's own N.L darkens (a smooth normal can still face the sun a little there, and the terminator of a
// faceted mesh would otherwise step from facet to facet). `cascade` is the cascade read, -1 for none.
float FenrirSunShadow(vec3 position, vec3 normal, vec3 l, out int cascade)
{
    float nDotL = dot(normal, l);
    float viewDepth = -(view.view * vec4(position, 1.0)).z;
    cascade = view.shadowInfo.x == 0u ? -1 : FenrirShadowCascade(viewDepth);
    if (cascade < 0 || nDotL <= 0.0)
    {
        return 1.0;
    }
    float angle = 6.28318531 * FenrirInterleavedGradientNoise(gl_FragCoord.xy);
    float shadow = FenrirCascadeShadow(cascade, position, normal, l, nDotL, angle);
    // The last share of the cascade's range blends into the next cascade, or into no shadow after the last.
    float start = cascade == 0 ? 0.0 : view.shadowSplits[cascade - 1];
    float end = view.shadowSplits[cascade];
    float band = view.shadowBias.w * (end - start);
    float blend = clamp((viewDepth - (end - band)) / max(band, 1e-6), 0.0, 1.0);
    if (blend > 0.0)
    {
        float next = cascade + 1 < int(view.shadowInfo.x)
                         ? FenrirCascadeShadow(cascade + 1, position, normal, l, nDotL, angle)
                         : 1.0;
        shadow = mix(shadow, next, blend);
    }
    return shadow;
}

// How much of spot light `index` of the light buffer reaches `position`, 0 to 1, for the face's world normal `normal`
// and `l` towards the light: 1 for a light without a shadow (index frame.lights.y or past it), on a face turned from the
// light, which its N.L darkens, and outside its tile (a cone wider than its shadow's 80 degrees).
float FenrirSpotLightShadow(uint index, vec3 position, vec3 normal, vec3 l)
{
    float nDotL = dot(normal, l);
    if (index >= frame.lights.y || nDotL <= 0.0)
    {
        return 1.0;
    }
    FenrirSpotShadow shadow = fenrirSpotShadows.items[index];
    // Clip w is the distance along the light's axis, which a texel's world size grows with.
    float along = (shadow.atlas * vec4(position, 1.0)).w;
    vec3 biased = FenrirShadowBiased(position, normal, l, nDotL, shadow.bias.z * max(along, 0.0), shadow.bias.x,
                                     shadow.bias.y);
    vec4 atlas = shadow.atlas * vec4(biased, 1.0);
    if (atlas.w <= 0.0)
    {
        return 1.0;
    }
    vec3 coords = atlas.xyz / atlas.w;
    if (any(lessThan(coords.xy, shadow.bounds.xy)) || any(greaterThan(coords.xy, shadow.bounds.zw)))
    {
        return 1.0;
    }
    vec2 texelUv = 1.0 / vec2(textureSize(fenrirSpotShadowAtlas, 0));
    float angle = 6.28318531 * FenrirInterleavedGradientNoise(gl_FragCoord.xy);
    return FenrirFilteredShadow(fenrirSpotShadowAtlas, coords, shadow.bounds.xy, shadow.bounds.zw, texelUv,
                                shadow.bias.w, angle);
}

// How much of point light `index` of the light buffer reaches `position`, 0 to 1, for the face's world normal `normal`
// and `l` towards the light: 1 for a light without a shadow (outside the shadowed point lights' records, frame.lights.y
// to y + z) and on a face turned from the light, which its N.L darkens.
float FenrirPointLightShadow(uint index, vec3 position, vec3 normal, vec3 l)
{
    float nDotL = dot(normal, l);
    uint first = frame.lights.y;
    if (index < first || index >= first + frame.lights.z || nDotL <= 0.0)
    {
        return 1.0;
    }
    FenrirLight light = fenrirLights.items[index];
    FenrirPointShadow shadow = fenrirPointShadows.items[index - first];
    // A texel's world size grows with the distance along the face's axis: the largest of the way's components.
    vec3 away = position - light.position;
    float along = max(abs(away.x), max(abs(away.y), abs(away.z)));
    vec3 biased = FenrirShadowBiased(position, normal, l, nDotL, shadow.bias.z * along, shadow.bias.x, shadow.bias.y);
    vec3 way = biased - light.position;
    float reference = 1.0 - length(way) / light.range;
    float texel = shadow.bias.z * max(abs(way.x), max(abs(way.y), abs(way.z)));
    // The disk across the way: two directions perpendicular to it.
    vec3 axis = normalize(way);
    vec3 side = normalize(cross(abs(axis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), axis));
    vec3 up = cross(axis, side);
    float angle = 6.28318531 * FenrirInterleavedGradientNoise(gl_FragCoord.xy);
    float cube = float(index - first);
    float lit = 0.0;
    for (int i = 0; i < FenrirShadowTaps; ++i)
    {
        vec2 offset = FenrirVogelDisk(i, FenrirShadowTaps, angle) * shadow.bias.w * texel;
        // GLSL has no explicit level for a cube array comparison; its one level makes the implicit one level 0.
        lit += texture(fenrirPointShadowCubes, vec4(way + side * offset.x + up * offset.y, cube), reference);
    }
    return lit / float(FenrirShadowTaps);
}

// A tint per cascade for CameraDebugView::ShadowCascades: red, green, blue and yellow, white for none.
vec3 FenrirCascadeTint(int cascade)
{
    const vec3 tints[4] = vec3[](vec3(1.0, 0.3, 0.3), vec3(0.3, 1.0, 0.3), vec3(0.3, 0.45, 1.0), vec3(1.0, 1.0, 0.3));
    return cascade < 0 ? vec3(1.0) : tints[cascade];
}

#endif
