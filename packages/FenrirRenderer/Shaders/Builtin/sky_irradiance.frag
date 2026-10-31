#version 460
#extension GL_GOOGLE_include_directive : require
// The SkyIrradiance pass (ADR-0047): the scene's Sky cube projected onto L2 spherical harmonics, convolved with the
// clamped cosine and divided by pi, into a 9 x 1 target, one coefficient a texel. Each texel reads every texel of one
// mip level of the cube, at its centre, weighted by its solid angle. The CPU mirror is Core/SphericalHarmonics.cpp's
// ProjectSkyIrradiance, kept step for step with this.
#include "Fenrir/CubeFaces.glsl"

layout(set = 2, binding = 0, std140) uniform SkyIrradianceParams
{
    // The side of the cube's mip level read.
    uint side;
    // That mip level.
    float lod;
} params;

layout(set = 2, binding = 1) uniform samplerCube sky;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

// The solid angle a face spans from its centre to the point (x, y).
float CornerArea(float x, float y)
{
    return atan(x * y, sqrt(x * x + y * y + 1.0));
}

// The real L2 basis function `index` at the unit direction d.
float Basis(int index, vec3 d)
{
    switch (index)
    {
    case 0:
        return 0.282095;
    case 1:
        return 0.488603 * d.y;
    case 2:
        return 0.488603 * d.z;
    case 3:
        return 0.488603 * d.x;
    case 4:
        return 1.092548 * d.x * d.y;
    case 5:
        return 1.092548 * d.y * d.z;
    case 6:
        return 0.315392 * (3.0 * d.z * d.z - 1.0);
    case 7:
        return 1.092548 * d.x * d.z;
    default:
        return 0.546274 * (d.x * d.x - d.y * d.y);
    }
}

void main()
{
    int index = int(gl_FragCoord.x);
    float texel = 2.0 / float(params.side);
    vec3 sum = vec3(0.0);
    for (uint face = 0u; face < 6u; ++face)
    {
        for (uint row = 0u; row < params.side; ++row)
        {
            for (uint column = 0u; column < params.side; ++column)
            {
                float x0 = -1.0 + texel * float(column);
                float y0 = -1.0 + texel * float(row);
                float x1 = x0 + texel;
                float y1 = y0 + texel;
                float solidAngle = CornerArea(x0, y0) - CornerArea(x0, y1) - CornerArea(x1, y0) + CornerArea(x1, y1);
                vec3 direction = normalize(FenrirFaceDirection(face, vec2(x0 + x1, y0 + y1) * 0.5));
                sum += textureLod(sky, direction, params.lod).rgb * solidAngle * Basis(index, direction);
            }
        }
    }
    // The clamped cosine's bands divided by pi: 1, 2 / 3 and 1 / 4.
    float band = index == 0 ? 1.0 : (index < 4 ? 2.0 / 3.0 : 0.25);
    outColour = vec4(sum * band, 1.0);
}
