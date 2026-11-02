#version 460
#extension GL_GOOGLE_include_directive : require
// LightingDemo's building facades, fragment stage: windows laid out on each wall, lit by the building's copy colour,
// and the whole lit through Fenrir/Lighting.glsl, so the street lamps, the neon signs and the headlights light the walls
// and shine in the glass. A wall is divided into bays between corner piers and into storeys above a taller shop floor;
// each bay of a storey is a window. Whether a window is lit, and how brightly, is a hash of the building (its centre),
// the wall, the bay and the storey: lit when under the copy colour's alpha, the building's share of lit windows, nudged
// up or down a little per storey so whole floors tend to be lit or dark together. A lit window glows the copy colour's
// RGB times windowStrength, varied per window; shop windows are lit more often and brighter. An unlit window is dark,
// glossy glass. Roofs are plain and rough.
//
// Far off, a window covers too few pixels for its edges or its hashed light to hold still: as a bay shrinks below a
// few pixels, each window's own mask and light give way to their averages over the wall, so a distant facade glows
// evenly instead of shimmering.
#include "Fenrir/Lighting.glsl"

#pragma fenrir default(wallTint, 1.0, 1.0, 1.0, 1.0)
#pragma fenrir default(windowStrength, 0.12)
#pragma fenrir default(wallRoughness, 0.75)
#pragma fenrir default(glassRoughness, 0.06)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA, times each building's wall colour.
    vec4 wallTint;
    // How bright a lit window is, times its copy colour.
    float windowStrength;
    float wallRoughness;
    float glassRoughness;
} material;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColour;
layout(location = 3) in vec3 vLocal;
layout(location = 4) flat in vec3 vHalfSize;
layout(location = 5) flat in vec3 vCentre;
layout(location = 0) out vec4 outColour;

// The wall's piers at each corner, a bay's width aimed for, the shop floor's height, a storey's and the parapet's, in
// metres.
const float Pier = 0.7;
const float BayWidth = 2.6;
const float ShopHeight = 4.8;
const float StoreyHeight = 3.4;
const float Parapet = 1.2;

// 0 to 1, from four integers.
float Hash(uvec4 v)
{
    uint h = v.x * 0x8da6b343u ^ v.y * 0xd8163841u ^ v.z * 0xcb1ab31fu ^ v.w * 0x165667b1u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return float(h) / 4294967295.0;
}

// How much of a window spanning `from` to `to` of its cell covers the cell coordinate `x`, softened over `width`, the
// coordinate's change across the pixel.
float Span(float x, float width, float from, float to)
{
    float w = max(width, 1e-4);
    return clamp((x - from) / w + 0.5, 0.0, 1.0) * clamp((to - x) / w + 0.5, 0.0, 1.0);
}

void main()
{
    vec3 n = normalize(vNormal);
    ivec2 lot = ivec2(floor(vCentre.xz * 4.0));
    uint building = uint(lot.x) * 7919u + uint(lot.y) * 104729u;
    vec3 wall = mix(vec3(0.045, 0.045, 0.05), vec3(0.17, 0.155, 0.14), Hash(uvec4(building, 1u, 0u, 0u))) *
                material.wallTint.rgb;

    // Across the wall and up it, in metres: u from one side of the wall to the other, height from the ground.
    bool alongZ = abs(n.x) > abs(n.z);
    float u = alongZ ? vLocal.z : vLocal.x;
    float halfWidth = alongZ ? vHalfSize.z : vHalfSize.x;
    uint face = alongZ ? (n.x > 0.0 ? 0u : 1u) : (n.z > 0.0 ? 2u : 3u);
    float height = vLocal.y + vHalfSize.y;
    float top = 2.0 * vHalfSize.y - Parapet;

    float span = max(2.0 * (halfWidth - Pier), 0.0);
    float bays = max(floor(span / BayWidth), 1.0);
    float bay = (u + halfWidth - Pier) / (span / bays);
    bool shop = height < ShopHeight;
    float storey = shop ? height / ShopHeight : (height - ShopHeight) / StoreyHeight;
    // Taken here, in uniform control flow, for the masks below. The storey's from the height, which runs on smoothly
    // where the storey's formula changes at the shop floor's top.
    float bayWidth = fwidth(bay);
    float storeyWidth = fwidth(height) / (shop ? ShopHeight : StoreyHeight);

    // The window of this bay and storey, and how much of the pixel it covers.
    float column = floor(bay);
    float row = shop ? 0.0 : floor(storey) + 1.0;
    bool inside = abs(n.y) <= 0.5 && bay >= 0.0 && bay < bays && height < top;
    float mask = shop ? Span(fract(bay), bayWidth, 0.05, 0.95) * Span(storey, storeyWidth, 0.06, 0.72)
                      : Span(fract(bay), bayWidth, 0.14, 0.86) * Span(fract(storey), storeyWidth, 0.22, 0.84);
    float coverage = shop ? 0.9 * 0.66 : 0.72 * 0.62;

    uvec4 cell = uvec4(building, face, uint(column + 64.0), uint(row));
    float share = vColour.a + (Hash(uvec4(building, face, 99u, uint(row))) - 0.5) * 0.35;
    share = clamp(shop ? share + 0.35 : share, 0.0, 0.95);
    float lit = Hash(cell) < share ? 1.0 : 0.0;
    float brightness = mix(0.35, 1.25, Hash(cell + uvec4(0u, 0u, 0u, 4096u))) * (shop ? 1.6 : 1.0);
    vec3 light = vColour.rgb * material.windowStrength;

    // Each window's own mask and light, giving way to the wall's averages as its bay falls under a few pixels.
    float near = smoothstep(2.0, 6.0, 1.0 / max(max(bayWidth, storeyWidth), 1e-5));
    float glass = inside ? mix(coverage, mask, near) : 0.0;
    float average = coverage * share * (shop ? 1.6 : 0.8);
    vec3 glow = inside ? light * mix(average, mask * lit * brightness, near) : vec3(0.0);

    FenrirSurface surface;
    surface.alpha = 1.0;
    surface.normal = n;
    surface.position = vWorldPosition;
    surface.metallic = 0.0;
    surface.occlusion = 1.0;
    surface.emissive = glow;
    if (abs(n.y) > 0.5)
    {
        // The roof.
        surface.baseColour = vec3(0.035);
        surface.roughness = 0.9;
    }
    else
    {
        surface.baseColour = mix(wall, vec3(0.015, 0.017, 0.02), glass);
        surface.roughness = mix(material.wallRoughness, material.glassRoughness, glass);
    }
    outColour = FenrirLitColour(surface);
}
