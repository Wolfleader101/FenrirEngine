#version 460
#extension GL_GOOGLE_include_directive : require
// The SkySpecular passes (ADR-0047): the scene's Sky cube prefiltered with GGX into one face of one mip level of the
// specular cube, one pass a face and level, mip k holding roughness k / (levels - 1). Each texel takes the GGX lobe
// round its direction (N = V = R, as the split sum assumes) by importance sampling, each sample read from the source mip
// whose texels span about the sample's share of the lobe's solid angle (Karis 2013, after Colbert and Krivanek), so a
// few samples give a smooth result. Roughness 0 copies the source at the level's own resolution.
#include "Fenrir/CubeFaces.glsl"

layout(set = 2, binding = 0, std140) uniform SkySpecularParams
{
    // The face drawn, in a cube texture's order: +X, -X, +Y, -Y, +Z, -Z.
    uint face;
    // This level's perceptual roughness.
    float roughness;
    // The side of the source cube's mip 0, and its highest mip level.
    float sourceSide;
    float sourceMaxLod;
    // The side of this level.
    float side;
} params;

layout(set = 2, binding = 1) uniform samplerCube sky;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

const float Pi = 3.14159265358979;
const uint SampleCount = 128u;

// The i-th of `count` points of the Hammersley set in [0, 1)^2.
vec2 Hammersley(uint i, uint count)
{
    return vec2(float(i) / float(count), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

// A half vector about +Z sampled from GGX's distribution of D(h) (h.n), with a = roughness squared.
vec3 SampleGgx(vec2 xi, float a)
{
    float phi = 2.0 * Pi * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

void main()
{
    vec3 n = normalize(FenrirFaceDirection(params.face, vNdc));
    if (params.roughness <= 0.0)
    {
        float lod = clamp(log2(params.sourceSide / params.side), 0.0, params.sourceMaxLod);
        outColour = vec4(textureLod(sky, n, lod).rgb, 1.0);
        return;
    }

    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, n));
    vec3 bitangent = cross(n, tangent);
    float a = params.roughness * params.roughness;
    // The solid angle of one source texel at mip 0.
    float texelSolidAngle = 4.0 * Pi / (6.0 * params.sourceSide * params.sourceSide);

    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < SampleCount; ++i)
    {
        vec3 h = SampleGgx(Hammersley(i, SampleCount), a);
        h = tangent * h.x + bitangent * h.y + n * h.z;
        vec3 l = normalize(2.0 * dot(n, h) * h - n);
        float nDotL = dot(n, l);
        if (nDotL <= 0.0)
        {
            continue;
        }
        // With N = V, the sample's pdf over l is D(h) / 4.
        float nDotH = max(dot(n, h), 0.0);
        float d2 = nDotH * nDotH * (a * a - 1.0) + 1.0;
        float pdf = a * a / (Pi * d2 * d2) / 4.0;
        float sampleSolidAngle = 1.0 / (float(SampleCount) * pdf + 1e-4);
        float lod = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0, params.sourceMaxLod);
        sum += textureLod(sky, l, lod).rgb * nDotL;
        weight += nDotL;
    }
    outColour = vec4(sum / max(weight, 1e-4), 1.0);
}
