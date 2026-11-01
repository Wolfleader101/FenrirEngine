// Fenrir/Shadows.glsl: the sun's cascaded shadow (ADR-0050), from the view block's cascades and shadow atlas
// (Fenrir/View.glsl). Fenrir/Lighting.glsl darkens the directional light by it; a shader may call it itself.
//
// Each cascade is a texel-snapped orthographic view of the sun, so its texels stay on the world's grid as the camera
// moves. A pixel reads the first cascade whose depth range holds it, and in the last share of that range (the view
// block's shadowBias.w) blends towards the next, the last fading out, so no boundary shows. Each read is hardware
// depth comparison, bilinear, at 16 taps of a Vogel disk turned per pixel by interleaved gradient noise (Jimenez 2014),
// softer than a grid at the same cost, the noise breaking the banding a fixed pattern leaves. The pixel is moved before
// the read along its normal by the normal-offset bias and towards the sun by the slope-scaled bias, both in the
// cascade's own texels, so neither grows or shrinks with the cascades.
#ifndef FENRIR_SHADOWS_GLSL
#define FENRIR_SHADOWS_GLSL

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

// How much of the sun reaches `position` through cascade `cascade`, 0 to 1, for the world normal `normal` facing the
// sun at N.L `nDotL` along `l` (towards the sun), the disk turned by `angle`.
float FenrirCascadeShadow(int cascade, vec3 position, vec3 normal, vec3 l, float nDotL, float angle)
{
    float texel = view.shadowTexels[cascade];
    float slope = min(sqrt(max(1.0 - nDotL * nDotL, 0.0)) / max(nDotL, 1e-4), FenrirShadowMaxSlope);
    vec3 biased = position + normal * (view.shadowBias.x * texel) + l * (view.shadowBias.y * texel * max(slope, 0.1));
    vec3 atlas = (view.shadowMatrices[cascade] * vec4(biased, 1.0)).xyz;

    // Read only inside the cascade's own tile of the atlas, the filter's reach in from its edges.
    vec2 size = vec2(textureSize(fenrirShadowMap, 0));
    float tiles = float(view.shadowInfo.x);
    vec2 texelUv = 1.0 / size;
    float reach = view.shadowBias.z + 1.0;
    vec2 low = vec2(float(cascade) / tiles, 0.0) + reach * texelUv;
    vec2 high = vec2(float(cascade + 1) / tiles, 1.0) - reach * texelUv;

    float lit = 0.0;
    for (int i = 0; i < FenrirShadowTaps; ++i)
    {
        vec2 offset = FenrirVogelDisk(i, FenrirShadowTaps, angle) * view.shadowBias.z * texelUv;
        // Level 0 explicitly: the caller may be in non-uniform control flow.
        lit += textureLod(fenrirShadowMap, vec3(clamp(atlas.xy + offset, low, high), atlas.z), 0.0);
    }
    return lit / float(FenrirShadowTaps);
}

// The cascade `position` reads, -1 past the last; `viewDepth` is its distance along the camera's axis.
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

// A tint per cascade for CameraDebugView::ShadowCascades: red, green, blue and yellow, white for none.
vec3 FenrirCascadeTint(int cascade)
{
    const vec3 tints[4] = vec3[](vec3(1.0, 0.3, 0.3), vec3(0.3, 1.0, 0.3), vec3(0.3, 0.45, 1.0), vec3(1.0, 1.0, 0.3));
    return cascade < 0 ? vec3(1.0) : tints[cascade];
}

#endif
