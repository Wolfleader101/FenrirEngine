#version 460
#extension GL_GOOGLE_include_directive : require
// LightingDemo's building facades, fragment stage: walls of fetched Poly Haven materials with windows set into them,
// rooms behind the windows, and the whole lit through Fenrir/Lighting.glsl, so the street lamps, the neon signs, the
// uplights and the headlights light the walls and shine in the glass.
//
// The copy colour's RGB is the light in the rooms. Its alpha packs three things: its fraction is the share of rooms
// lit, its whole part the facade's style (0, 1 or 2) plus 3 when the tier has neon edge strips.
//
// - Style 0, punched windows: piers at each corner, one window a bay a storey, set back in the wall with a dark frame,
//   a shadowed reveal and a sill; brick or cladding, chosen per tier.
// - Style 1, a curtain wall: glass from mullion to mullion over dark concrete spandrels, with metal mullions; rooms lit
//   a floor at a time in runs of bays, as offices are.
// - Style 2, ribbon windows: a band of glass along each storey between concrete or cladding bands, lit in pairs of bays.
//
// The walls are textured by world position at their materials' real sizes, normal mapped in the wall's frame (u along
// the wall, v up), streaked darker under each window and towards the ground. Storeys are counted from the ground, above
// a taller shop floor, so a tower's tiers keep their floors in line; only a tier standing on the ground has the shop
// floor, whose deeper rooms are lit more often and brighter.
//
// Behind each window is a room (interior mapping): the ray through the glass is followed into a box the bay's width,
// the storey's height and a few metres deep, and what it meets is shaded: a back wall with desks along it, side walls,
// a floor, and a ceiling with a light panel whose light falls off across the room. So the rooms shift with parallax as
// the camera passes. A lit room glows its copy colour (one in sixteen a neon hue instead) times windowStrength; an unlit
// one is all but dark. Some windows have blinds partly drawn, lit from behind. Whether a room is lit, and how brightly,
// is a hash of the tier (its centre), the wall, the bay and the storey, nudged per storey so whole floors tend to be lit
// or dark together. The glass itself is dark and glossy, so the lights outside shine in it too.
//
// A tier with edge strips glows a neon hue (a hash of the tier) up each corner and along its crown, accentStrength
// bright: the city's colour from far off.
//
// Far off, a window covers too few pixels for its edges, its room or its hashed light to hold still: as a bay shrinks
// below a few pixels, each window's own mask and room give way to their averages over the wall, so a distant facade
// glows evenly instead of shimmering.
#include "Fenrir/Lighting.glsl"

#pragma fenrir default(wallTint, 1.0, 1.0, 1.0, 1.0)
#pragma fenrir default(tiles, 2.09, 1.05, 2.0, 0.0)
#pragma fenrir default(windowStrength, 0.25)
#pragma fenrir default(accentStrength, 2.0)
#pragma fenrir default(glassRoughness, 0.06)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA, times every wall's base colour.
    vec4 wallTint;
    // Metres per repeat of the concrete's, the brick's and the cladding's textures; w unused.
    vec4 tiles;
    // How bright a lit room is, times its copy colour.
    float windowStrength;
    // How bright the neon edge strips are.
    float accentStrength;
    float glassRoughness;
} material;
#pragma fenrir default(concreteColour, white)
layout(set = 2, binding = 1) uniform sampler2D concreteColour;
#pragma fenrir default(concreteNormal, normal)
layout(set = 2, binding = 2) uniform sampler2D concreteNormal;
#pragma fenrir default(concreteArm, white)
layout(set = 2, binding = 3) uniform sampler2D concreteArm;
#pragma fenrir default(brickColour, white)
layout(set = 2, binding = 4) uniform sampler2D brickColour;
#pragma fenrir default(brickNormal, normal)
layout(set = 2, binding = 5) uniform sampler2D brickNormal;
#pragma fenrir default(brickArm, white)
layout(set = 2, binding = 6) uniform sampler2D brickArm;
#pragma fenrir default(claddingColour, white)
layout(set = 2, binding = 7) uniform sampler2D claddingColour;
#pragma fenrir default(claddingNormal, normal)
layout(set = 2, binding = 8) uniform sampler2D claddingNormal;

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
// The edge strips' width, a window frame's and a reveal's shadow, in metres.
const float StripWidth = 0.3;
const float FrameWidth = 0.07;
const float RevealWidth = 0.14;
// How deep an office's room is, and a shop's, in metres.
const float RoomDepth = 5.0;
const float ShopDepth = 8.0;

// The neon hues of the edge strips and of the odd coloured room.
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

// What a ray from `p` on the glass, along `d`, sees of the room behind it, unlit by the scene: x across the bay, y up
// the storey, z into the room, in metres; the room is `size` (width, height, depth). It is lit by a ceiling panel in its
// middle, whose own face returns a bright white; the rest is the surface's colour times how much of the panel's light
// reaches it. `seed` varies the walls' colour and the desks.
vec3 Room(vec3 p, vec3 d, vec3 size, uint seed)
{
    vec3 dir = vec3(abs(d.x) < 1e-4 ? 1e-4 : d.x, abs(d.y) < 1e-4 ? 1e-4 : d.y, max(d.z, 1e-3));
    vec3 far = vec3(dir.x > 0.0 ? size.x : 0.0, dir.y > 0.0 ? size.y : 0.0, size.z);
    vec3 t = (far - p) / dir;
    float hit = min(min(t.x, t.y), t.z);
    vec3 q = p + dir * hit;

    vec3 paint = mix(vec3(0.62, 0.58, 0.52), vec3(Hash(uvec4(seed, 1u, 0u, 0u)), Hash(uvec4(seed, 2u, 0u, 0u)),
                                                      Hash(uvec4(seed, 3u, 0u, 0u))),
                     0.3);
    vec3 albedo;
    if (hit == t.z)
    {
        // The back wall, darker low down, with a row of desks along it in most rooms.
        albedo = paint * mix(0.7, 1.0, q.y / size.y);
        bool desks = Hash(uvec4(seed, 4u, 0u, 0u)) < 0.7;
        if (desks && q.y < 0.75 && abs(q.x - size.x * 0.5) < size.x * 0.4)
        {
            albedo = vec3(0.08, 0.07, 0.06);
        }
    }
    else if (hit == t.x)
    {
        albedo = paint * 0.8;
    }
    else if (dir.y < 0.0)
    {
        // The floor.
        albedo = vec3(0.12, 0.11, 0.1);
    }
    else
    {
        // The ceiling, with its light panel.
        vec2 fromMiddle = abs(q.xz - vec2(size.x * 0.5, size.z * 0.5));
        if (fromMiddle.x < 0.55 && fromMiddle.y < 0.3)
        {
            return vec3(6.0);
        }
        albedo = vec3(0.7);
    }
    vec3 toLamp = q - vec3(size.x * 0.5, size.y, size.z * 0.5);
    return albedo * (0.2 + 1.6 / (1.0 + dot(toLamp, toLamp) * 0.15));
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
    bool side = abs(n.y) <= 0.5;

    // The wall's frame: t along it (to the right, seen from outside), up, and n out of it. Roofs use X and -Z.
    vec3 wallNormal = abs(n.x) > abs(n.z) ? vec3(sign(n.x), 0.0, 0.0) : vec3(0.0, 0.0, sign(n.z));
    vec3 t = cross(vec3(0.0, 1.0, 0.0), wallNormal);
    float u = dot(vWorldPosition - vCentre, t);
    float halfWidth = abs(t.x) > 0.5 ? vHalfSize.x : vHalfSize.z;
    uint face = abs(n.x) > abs(n.z) ? (n.x > 0.0 ? 0u : 1u) : (n.z > 0.0 ? 2u : 3u);
    float height = vWorldPosition.y - Kerb;
    float base = vCentre.y - vHalfSize.y - Kerb;
    float top = vCentre.y + vHalfSize.y - Kerb;

    // The wall's material, sampled here in uniform control flow: concrete, brick or cladding.
    vec2 wallUv = side ? vec2(dot(vWorldPosition, t), vWorldPosition.y) : vec2(vWorldPosition.x, -vWorldPosition.z);
    wallUv += vec2(Hash(uvec4(tier, 6u, 0u, 0u)), Hash(uvec4(tier, 7u, 0u, 0u))) * 8.0;
    // The tiles are charcoal; lifted to a weathered grey, so a tower's walls read under the city's light.
    vec3 concrete = texture(concreteColour, wallUv / material.tiles.x).rgb * 2.5;
    vec3 concreteN = texture(concreteNormal, wallUv / material.tiles.x).xyz * 2.0 - 1.0;
    vec3 concreteA = texture(concreteArm, wallUv / material.tiles.x).rgb;
    vec3 brick = texture(brickColour, wallUv / material.tiles.y).rgb;
    vec3 brickN = texture(brickNormal, wallUv / material.tiles.y).xyz * 2.0 - 1.0;
    vec3 brickA = texture(brickArm, wallUv / material.tiles.y).rgb;
    vec3 cladding = texture(claddingColour, wallUv / material.tiles.z).rgb;
    vec3 claddingN = texture(claddingNormal, wallUv / material.tiles.z).xyz * 2.0 - 1.0;

    float pier = style == 0 ? Pier : 0.25;
    float span = max(2.0 * (halfWidth - pier), 0.0);
    float bays = max(floor(span / BayWidth), 1.0);
    float bayMetres = span / bays;
    float bay = (u + halfWidth - pier) / bayMetres;
    bool shop = base < 1.0 && height < ShopHeight;
    float storeyMetres = shop ? ShopHeight : StoreyHeight;
    float storey = shop ? height / ShopHeight : (height - ShopHeight) / StoreyHeight;
    // Taken here, in uniform control flow, for the masks below. The storey's from the height, which runs on smoothly
    // where the storey's formula changes at the shop floor's top.
    float bayWidth = fwidth(bay);
    float storeyWidth = fwidth(height) / storeyMetres;
    float uWidth = fwidth(u);
    float heightWidth = fwidth(height);

    // This bay and storey's window: its opening, the glass inside its frame, and the reveal's shadow round it, each as
    // a share of the pixel, from the window's place in its cell (across, 0 to 1, and up, 0 to 1).
    float column = floor(bay);
    float row = shop ? 0.0 : floor(storey) + 1.0;
    float across = fract(bay);
    float up = shop ? storey : fract(storey);
    bool inside = side && bay >= 0.0 && bay < bays && height < top - Parapet && height > base + 0.2;
    vec2 openX = style == 0 ? vec2(0.14, 0.86) : (style == 1 ? vec2(0.04, 0.96) : vec2(0.02, 0.98));
    vec2 openY = style == 0 ? vec2(0.22, 0.84) : (style == 1 ? vec2(0.1, 0.92) : vec2(0.3, 0.78));
    openX = shop ? vec2(0.05, 0.95) : openX;
    openY = shop ? vec2(0.06, 0.72) : openY;
    vec2 frame = vec2(FrameWidth / bayMetres, FrameWidth / storeyMetres);
    vec2 reveal = vec2(RevealWidth / bayMetres, RevealWidth / storeyMetres);
    float opening = Span(across, bayWidth, openX.x, openX.y) * Span(up, storeyWidth, openY.x, openY.y);
    float glassMask = Span(across, bayWidth, openX.x + frame.x, openX.y - frame.x) *
                      Span(up, storeyWidth, openY.x + frame.y, openY.y - frame.y);
    float shadow = inside ? Span(across, bayWidth, openX.x - reveal.x, openX.y + reveal.x) *
                                    Span(up, storeyWidth, openY.x - reveal.y, openY.y + reveal.y) -
                                opening
                          : 0.0;
    float sill = inside && style == 0 && !shop ? Span(across, bayWidth, openX.x - frame.x, openX.y + frame.x) *
                                           Span(up, storeyWidth, openY.x - 0.12 / storeyMetres, openY.x)
                                     : 0.0;

    // Whether the room is lit, how brightly, and in what light.
    float run = style == 1 ? floor(column / 4.0) : (style == 2 ? floor(column / 2.0) : column);
    uvec4 cell = uvec4(tier, face, uint(run + 64.0), uint(row));
    float floorShare = share + (Hash(uvec4(tier, face, 99u, uint(row))) - 0.5) * 0.35;
    floorShare = clamp(shop ? floorShare + 0.35 : floorShare, 0.0, 0.95);
    float lit = Hash(cell) < floorShare ? 1.0 : 0.04;
    float brightness = mix(0.4, 1.2, Hash(cell + uvec4(0u, 0u, 0u, 4096u))) * (shop ? 1.6 : 1.0);
    float hued = Hash(cell + uvec4(0u, 0u, 0u, 8192u));
    vec3 light = hued < 0.06 ? Neon[int(hued * 100.0) % 6] : vColour.rgb;

    // The room behind the glass, by the view ray in the window's frame (x along t, y up, z into the building).
    vec3 toPixel = normalize(vWorldPosition - view.cameraPosition.xyz);
    vec3 ray = vec3(dot(toPixel, t), toPixel.y, -dot(toPixel, wallNormal));
    vec3 room = vec3(bayMetres, storeyMetres, shop ? ShopDepth : RoomDepth);
    uint seed = cell.x ^ (cell.z * 2654435761u) ^ (uint(column) * 40503u) ^ (cell.w * 2246822519u);
    vec3 interior = Room(vec3(across * room.x, up * room.y, 0.0), ray, room, seed);
    // Blinds drawn part way down some windows, lit from behind, their slats catching the light.
    float drawn = 0.2 + 0.6 * Hash(cell + uvec4(0u, 0u, 0u, 16384u));
    float blindsDown = Hash(cell + uvec4(0u, 0u, 0u, 12288u)) < 0.3 ? mix(openY.y, openY.x, drawn) : openY.y;
    if (up > blindsDown)
    {
        interior = vec3(0.55, 0.5, 0.42) * (0.85 + 0.15 * step(0.5, fract(up * room.y * 12.0)));
    }
    vec3 glow = interior * light * material.windowStrength * lit * brightness;

    // Each window's own room and edges, giving way to the wall's averages as its bay falls under a few pixels.
    float near = smoothstep(2.0, 6.0, 1.0 / max(max(bayWidth, storeyWidth), 1e-5));
    float coverage = (openX.y - openX.x) * (openY.y - openY.x);
    float average = 0.55 * floorShare * (shop ? 1.6 : 0.8);
    float glass = inside ? mix(coverage, glassMask, near) : 0.0;
    float frameShare = inside ? (opening - glassMask) * near : 0.0;
    vec3 emissive = inside ? mix(vColour.rgb * material.windowStrength * average * coverage, glow * glassMask, near)
                           : vec3(0.0);

    // The edge strips: up each corner of the wall, and along its crown.
    if (strips && side)
    {
        vec3 hue = Neon[int(Hash(uvec4(tier, 5u, 0u, 0u)) * 6.0) % 6];
        float corner = clamp((StripWidth - (halfWidth - abs(u))) / max(uWidth, 1e-4) + 0.5, 0.0, 1.0);
        float crown = Span(height, heightWidth, top - 0.5, top - 0.5 + StripWidth);
        emissive += hue * material.accentStrength * max(corner * step(base + 0.5, height), crown);
    }

    // The wall: brick or cladding for punched windows, concrete spandrels between curtain walls, concrete or cladding
    // bands between ribbons; roofs are concrete.
    float pick = Hash(uvec4(tier, 8u, 0u, 0u));
    vec3 wallColour = concrete;
    vec3 wallNormalMap = concreteN;
    vec3 wallArm = concreteA;
    if (side && style == 0)
    {
        wallColour = pick < 0.55 ? brick : cladding * 0.55;
        wallNormalMap = pick < 0.55 ? brickN : claddingN;
        wallArm = pick < 0.55 ? brickA : vec3(1.0, 0.8, 0.0);
    }
    else if (side && style == 2 && pick < 0.5)
    {
        wallColour = cladding * 0.45;
        wallNormalMap = claddingN;
        wallArm = vec3(1.0, 0.75, 0.0);
    }
    // Streaks of grime down from each window's sill, and the wall darker towards the ground.
    float below = inside ? Span(across, bayWidth, openX.x, openX.y) * step(up, openY.x) *
                               exp(-(openY.x - up) * storeyMetres * 0.6)
                         : 0.0;
    float grime = (1.0 - 0.3 * below * near) * mix(0.65, 1.0, clamp(height / 4.0, 0.0, 1.0));

    // The shading normal: the wall's map in its frame, the sill tipped up to catch the light, flat on the glass.
    mat3 frameOf = side ? mat3(t, vec3(0.0, 1.0, 0.0), wallNormal)
                        : mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, -1.0), vec3(0.0, 1.0, 0.0));
    vec3 mapped = normalize(frameOf * wallNormalMap);
    vec3 sillNormal = normalize(wallNormal + vec3(0.0, 0.8, 0.0));

    FenrirSurface surface;
    surface.alpha = 1.0;
    surface.position = vWorldPosition;
    surface.emissive = emissive;
    surface.normal = normalize(mix(mix(mapped, sillNormal, sill * near), n, max(glass, frameShare)));
    float wallRoughness = wallArm.g;
    float wallMetal = 0.0;
    vec3 wallBase = wallColour * grime * (1.0 - 0.5 * shadow * near) * material.wallTint.rgb;
    if (side && style == 1)
    {
        // The curtain wall's mullions and spandrels: dark anodised metal round the glass, concrete between floors.
        float mullion = inside ? Span(up, storeyWidth, openY.x - 0.05, openY.y + 0.05) : 0.0;
        wallBase = mix(wallBase, vec3(0.05, 0.055, 0.06), mullion);
        wallRoughness = mix(wallRoughness, 0.35, mullion);
        wallMetal = mullion * 0.8;
    }
    // The frame, dark metal; the sill, lighter stone.
    wallBase = mix(wallBase, vec3(0.04), frameShare);
    wallBase = mix(wallBase, vec3(0.3, 0.29, 0.27), sill * near * (1.0 - glass));
    surface.baseColour = mix(wallBase, vec3(0.012, 0.014, 0.018), glass);
    surface.roughness = mix(mix(wallRoughness, 0.4, frameShare), material.glassRoughness, glass);
    surface.metallic = mix(mix(wallMetal, 0.7, frameShare), 0.0, glass);
    surface.occlusion = mix(wallArm.r, 1.0, glass);
    outColour = FenrirLitColour(surface);
}
