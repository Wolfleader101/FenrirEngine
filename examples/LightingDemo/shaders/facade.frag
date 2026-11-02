#version 460
#extension GL_GOOGLE_include_directive : require
// LightingDemo's building facades, fragment stage: windows laid out on each wall of a building's tier, lit by the tier's
// copy colour, and the whole lit through Fenrir/Lighting.glsl, so the street lamps, the neon signs, the uplights and the
// headlights light the walls and shine in the glass. Roofs are plain and rough.
//
// The copy colour's RGB is the windows' light. Its alpha packs three things: its fraction is the share of windows lit,
// its whole part the facade's style (0, 1 or 2) plus 3 when the tier has neon edge strips.
//
// - Style 0, punched windows: piers at each corner, bays between them, one window a bay a storey, set in the wall.
// - Style 1, a curtain wall: glass from mullion to mullion and slab to slab, lit a floor at a time in runs of bays, as
//   offices are, with dark blue-grey mullions.
// - Style 2, ribbon windows: a band of glass along each storey, broken only by thin mullions, lit in pairs of bays.
//
// Storeys are counted from the ground, above a taller shop floor, so a tower's tiers keep their floors in line; only a
// tier standing on the ground has the shop floor, its windows lit more often and brighter. Whether a window is lit, and
// how brightly, is a hash of the tier (its centre), the wall, the bay and the storey: lit when under the share, nudged up
// or down per storey so whole floors tend to be lit or dark together. One lit window in sixteen takes a neon hue instead.
// An unlit window is dark, glossy glass.
//
// A tier with edge strips glows a neon hue (a hash of the tier) up each corner and along its crown, accentStrength
// bright: the city's colour from far off.
//
// Far off, a window covers too few pixels for its edges or its hashed light to hold still: as a bay shrinks below a
// few pixels, each window's own mask and light give way to their averages over the wall, so a distant facade glows
// evenly instead of shimmering.
#include "Fenrir/Lighting.glsl"

#pragma fenrir default(wallTint, 1.0, 1.0, 1.0, 1.0)
#pragma fenrir default(windowStrength, 0.12)
#pragma fenrir default(accentStrength, 2.0)
#pragma fenrir default(wallRoughness, 0.75)
#pragma fenrir default(glassRoughness, 0.06)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA, times each tier's wall colour.
    vec4 wallTint;
    // How bright a lit window is, times its copy colour.
    float windowStrength;
    // How bright the neon edge strips are.
    float accentStrength;
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
// metres; the kerb the city stands on.
const float Pier = 0.7;
const float BayWidth = 2.6;
const float ShopHeight = 4.8;
const float StoreyHeight = 3.4;
const float Parapet = 1.2;
const float Kerb = 0.15;
// The edge strips' width, in metres.
const float StripWidth = 0.3;

// The neon hues of the edge strips and of the odd coloured window.
const vec3 Neon[6] = vec3[](vec3(1.0, 0.08, 0.5), vec3(0.08, 0.8, 1.0), vec3(0.55, 0.15, 1.0), vec3(1.0, 0.4, 0.04),
                            vec3(0.15, 1.0, 0.3), vec3(0.12, 0.25, 1.0));

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

// How much of a span from `from` to `to` covers the coordinate `x`, softened over `width`, the coordinate's change
// across the pixel.
float Span(float x, float width, float from, float to)
{
    float w = max(width, 1e-4);
    return clamp((x - from) / w + 0.5, 0.0, 1.0) * clamp((to - x) / w + 0.5, 0.0, 1.0);
}

void main()
{
    vec3 n = normalize(vNormal);
    ivec3 key = ivec3(floor(vCentre * 4.0));
    uint tier = uint(key.x) * 7919u + uint(key.z) * 104729u + uint(key.y) * 15485863u;
    float whole = floor(vColour.a);
    float share = vColour.a - whole;
    int style = int(mod(whole, 3.0));
    bool strips = whole >= 3.0;

    // Across the wall and up it, in metres: u from one side of the wall to the other, height from the ground.
    bool alongZ = abs(n.x) > abs(n.z);
    float u = alongZ ? vLocal.z : vLocal.x;
    float halfWidth = alongZ ? vHalfSize.z : vHalfSize.x;
    uint face = alongZ ? (n.x > 0.0 ? 0u : 1u) : (n.z > 0.0 ? 2u : 3u);
    float height = vWorldPosition.y - Kerb;
    float base = vCentre.y - vHalfSize.y - Kerb;
    float top = vCentre.y + vHalfSize.y - Kerb;

    float pier = style == 0 ? Pier : 0.25;
    float span = max(2.0 * (halfWidth - pier), 0.0);
    float bays = max(floor(span / BayWidth), 1.0);
    float bay = (u + halfWidth - pier) / (span / bays);
    bool shop = base < 1.0 && height < ShopHeight;
    float storey = shop ? height / ShopHeight : (height - ShopHeight) / StoreyHeight;
    // Taken here, in uniform control flow, for the masks below. The storey's from the height, which runs on smoothly
    // where the storey's formula changes at the shop floor's top.
    float bayWidth = fwidth(bay);
    float storeyWidth = fwidth(height) / (shop ? ShopHeight : StoreyHeight);
    float uWidth = fwidth(u);
    float heightWidth = fwidth(height);

    // The window of this bay and storey, how much of the pixel it covers, and what share of the wall windows cover.
    float column = floor(bay);
    float row = shop ? 0.0 : floor(storey) + 1.0;
    bool side = abs(n.y) <= 0.5;
    bool inside = side && bay >= 0.0 && bay < bays && height < top - Parapet && height > base + 0.2;
    vec2 across = style == 0 ? vec2(0.14, 0.86) : (style == 1 ? vec2(0.04, 0.96) : vec2(0.02, 0.98));
    vec2 up = style == 0 ? vec2(0.22, 0.84) : (style == 1 ? vec2(0.1, 0.92) : vec2(0.3, 0.78));
    float mask = shop ? Span(fract(bay), bayWidth, 0.05, 0.95) * Span(storey, storeyWidth, 0.06, 0.72)
                      : Span(fract(bay), bayWidth, across.x, across.y) * Span(fract(storey), storeyWidth, up.x, up.y);
    float coverage = shop ? 0.9 * 0.66 : (across.y - across.x) * (up.y - up.x);

    // Offices are lit in runs of bays along a floor, ribbon windows in pairs, punched windows one by one.
    float run = style == 1 ? floor(column / 4.0) : (style == 2 ? floor(column / 2.0) : column);
    uvec4 cell = uvec4(tier, face, uint(run + 64.0), uint(row));
    float floorShare = share + (Hash(uvec4(tier, face, 99u, uint(row))) - 0.5) * 0.35;
    floorShare = clamp(shop ? floorShare + 0.35 : floorShare, 0.0, 0.95);
    float lit = Hash(cell) < floorShare ? 1.0 : 0.0;
    float brightness = mix(0.35, 1.25, Hash(cell + uvec4(0u, 0u, 0u, 4096u))) * (shop ? 1.6 : 1.0);
    float hued = Hash(cell + uvec4(0u, 0u, 0u, 8192u));
    vec3 light = vColour.rgb;
    if (hued < 0.06)
    {
        light = Neon[int(hued * 100.0) % 6];
    }
    light *= material.windowStrength;

    // Each window's own mask and light, giving way to the wall's averages as its bay falls under a few pixels.
    float near = smoothstep(2.0, 6.0, 1.0 / max(max(bayWidth, storeyWidth), 1e-5));
    float glass = inside ? mix(coverage, mask, near) : 0.0;
    float average = coverage * floorShare * (shop ? 1.6 : 0.8);
    vec3 glow = inside ? mix(vColour.rgb * material.windowStrength * average, light * mask * lit * brightness, near)
                       : vec3(0.0);

    // The edge strips: up each corner of the wall, and along its crown.
    if (strips && side)
    {
        vec3 hue = Neon[int(Hash(uvec4(tier, 5u, 0u, 0u)) * 6.0) % 6];
        float corner = clamp((StripWidth - (halfWidth - abs(u))) / max(uWidth, 1e-4) + 0.5, 0.0, 1.0);
        float crown = Span(height, heightWidth, top - 0.5, top - 0.5 + StripWidth);
        glow += hue * material.accentStrength * max(corner * step(base + 0.5, height), crown);
    }

    vec3 wall = style == 1 ? vec3(0.03, 0.035, 0.045)
                           : mix(vec3(0.045, 0.045, 0.05), vec3(0.17, 0.155, 0.14), Hash(uvec4(tier, 1u, 0u, 0u)));

    FenrirSurface surface;
    surface.alpha = 1.0;
    surface.normal = n;
    surface.position = vWorldPosition;
    surface.metallic = 0.0;
    surface.occlusion = 1.0;
    surface.emissive = glow;
    if (!side)
    {
        // The roof.
        surface.baseColour = vec3(0.035);
        surface.roughness = 0.9;
    }
    else
    {
        surface.baseColour = mix(wall * material.wallTint.rgb, vec3(0.015, 0.018, 0.024), glass);
        surface.roughness = mix(style == 1 ? 0.35 : material.wallRoughness, material.glassRoughness, glass);
    }
    outColour = FenrirLitColour(surface);
}
