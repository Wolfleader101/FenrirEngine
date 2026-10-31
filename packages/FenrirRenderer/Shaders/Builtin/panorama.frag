#version 460
#extension GL_GOOGLE_include_directive : require
// The Panorama passes (ADR-0047): a panorama sky's flat image drawn into one face of its cube texture at one mip level,
// one pass per face and level, once per image revision. Each texel takes the average of four points across it, each
// sampled from the panorama's mip whose texels are as tall as the cube's at this level, so every level is filtered from
// the panorama itself and no level is drawn from another.
#include "Fenrir/CubeFaces.glsl"

layout(set = 2, binding = 0, std140) uniform PanoramaParams
{
    // The face drawn, in a cube texture's order: +X, -X, +Y, -Y, +Z, -Z.
    uint face;
    // Half a texel of this level, in normalised device coordinates: 1 / its side.
    float halfTexel;
    // The panorama's mip level to sample.
    float lod;
} params;

layout(set = 2, binding = 1) uniform sampler2D panorama;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

const float Pi = 3.14159265358979;
// The largest finite half float: brighter texels (a sun's core, in some panoramas) are clamped, not made infinite.
const float HalfMax = 65504.0;

// Where the panorama holds the direction `direction`: its centre looks along -Z, u turns towards +X, and v = 0 (the top
// row) looks up +Y.
vec2 PanoramaPoint(vec3 direction)
{
    vec3 d = normalize(direction);
    return vec2(0.5 + atan(d.x, -d.z) / (2.0 * Pi), acos(clamp(d.y, -1.0, 1.0)) / Pi);
}

void main()
{
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; ++i)
    {
        vec2 offset = vec2((i & 1) == 0 ? -0.5 : 0.5, (i & 2) == 0 ? -0.5 : 0.5) * params.halfTexel;
        sum += textureLod(panorama, PanoramaPoint(FenrirFaceDirection(params.face, vNdc + offset)), params.lod).rgb;
    }
    outColour = vec4(min(sum * 0.25, vec3(HalfMax)), 1.0);
}
