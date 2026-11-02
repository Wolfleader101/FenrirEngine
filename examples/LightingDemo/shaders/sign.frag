#version 460
#extension GL_GOOGLE_include_directive : require
// LightingDemo's neon signs, fragment stage: a dark board with neon tubes on its faces, lit through
// Fenrir/Lighting.glsl so the board catches the lights round it. The tubes glow the copy colour's RGB: a tube round the
// board's edge, and glyphs inside it, one to a cell of a row along a wide board or a column down a tall one, as the
// blade signs of an Asian street are lettered. Each glyph is a few strokes, horizontal and vertical, picked by a hash of
// the board (its centre) and the cell, so no two signs read the same. A faint halo round each tube helps the bloom.
//
// The board's edges are dark. Far off, as a glyph's cell shrinks below a few pixels, the strokes give way to their
// average glow, so a distant sign holds still instead of shimmering.
#include "Fenrir/Lighting.glsl"

#pragma fenrir default(boardColour, 0.02, 0.02, 0.025, 1.0)
#pragma fenrir default(tubeWidth, 0.045)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA: the board between the tubes.
    vec4 boardColour;
    // A tube's width, in metres.
    float tubeWidth;
} material;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColour;
layout(location = 3) in vec3 vLocal;
layout(location = 4) flat in vec3 vHalfSize;
layout(location = 5) in vec3 vLocalNormal;
layout(location = 6) flat in vec3 vCentre;
layout(location = 0) out vec4 outColour;

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

// How much of a tube of half width `radius` along the line `x` = `at` covers the pixel, `width` being x's change
// across it; `from` to `to` is the tube's extent along the other coordinate `y`.
float Stroke(float x, float y, float at, float from, float to, float radius, vec2 width)
{
    float across = clamp((radius - abs(x - at)) / max(width.x, 1e-4) + 0.5, 0.0, 1.0);
    float along = clamp((y - from) / max(width.y, 1e-4) + 0.5, 0.0, 1.0) *
                  clamp((to - y) / max(width.y, 1e-4) + 0.5, 0.0, 1.0);
    return across * along;
}

void main()
{
    vec3 n = normalize(vNormal);
    // Its face, in metres from the board's centre: x across, y up.
    vec2 p = vLocal.xy;
    vec2 halfSize = vHalfSize.xy;
    // Taken here, in uniform control flow, for the strokes below.
    vec2 pWidth = fwidth(p);
    bool face = abs(vLocalNormal.z) > 0.5;
    ivec3 key = ivec3(floor(vCentre * 8.0));
    uint board = uint(key.x) * 7919u + uint(key.z) * 104729u + uint(key.y) * 15485863u;

    // The edge tube, inset from the board's rim.
    float tube = material.tubeWidth * 0.5;
    float inset = min(min(halfSize.x, halfSize.y) * 0.18, 0.12);
    vec2 edge = halfSize - inset;
    float rim = max(max(Stroke(p.x, p.y, -edge.x, -edge.y, edge.y, tube, pWidth.xy),
                        Stroke(p.x, p.y, edge.x, -edge.y, edge.y, tube, pWidth.xy)),
                    max(Stroke(p.y, p.x, -edge.y, -edge.x, edge.x, tube, pWidth.yx),
                        Stroke(p.y, p.x, edge.y, -edge.x, edge.x, tube, pWidth.yx)));

    // The glyphs: square-ish cells along the board's long side, inside the edge tube.
    bool tall = halfSize.y > halfSize.x;
    vec2 inner = edge - inset * 0.8;
    float cell = (tall ? inner.x : inner.y) * 2.0;
    float count = max(floor((tall ? inner.y : inner.x) * 2.0 / (cell * 0.9)), 1.0);
    float along = tall ? (inner.y - p.y) / (2.0 * inner.y) * count : (p.x + inner.x) / (2.0 * inner.x) * count;
    float index = clamp(floor(along), 0.0, count - 1.0);
    vec2 glyphSize = tall ? vec2(cell, 2.0 * inner.y / count) : vec2(2.0 * inner.x / count, cell);
    // The cell's own coordinates, 0 to 1 across and up, and their change across the pixel.
    vec2 c = tall ? vec2((p.x + inner.x) / glyphSize.x, 1.0 - fract(along))
                  : vec2(fract(along), (p.y + inner.y) / glyphSize.y);
    vec2 cWidth = pWidth / glyphSize;
    float strokeHalf = tube / min(glyphSize.x, glyphSize.y);
    float glyph = 0.0;
    uint glyphSeed = board ^ (uint(index) * 2654435761u);
    // Three horizontal and three vertical strokes, each kept or not, at least two kept, each spanning part of the cell.
    for (uint i = 0u; i < 6u; ++i)
    {
        float keep = Hash(uvec4(glyphSeed, i, 1u, 0u));
        if (keep < 0.5 && i > 1u)
        {
            continue;
        }
        float at = 0.18 + 0.32 * float(i % 3u);
        float from = 0.15 + 0.35 * Hash(uvec4(glyphSeed, i, 2u, 0u)) * 0.6;
        float to = 0.85 - 0.35 * Hash(uvec4(glyphSeed, i, 3u, 0u)) * 0.6;
        float s = i < 3u ? Stroke(c.y, c.x, at, from, to, strokeHalf, cWidth.yx)
                         : Stroke(c.x, c.y, at, from, to, strokeHalf, cWidth.xy);
        glyph = max(glyph, s);
    }
    bool inGlyphs = abs(p.x) < inner.x && abs(p.y) < inner.y;
    glyph = inGlyphs ? glyph : 0.0;

    // Far off, the glyphs' average; the rim holds up longer, being longer.
    float near = smoothstep(3.0, 8.0, 1.0 / max(max(cWidth.x, cWidth.y), 1e-5));
    float glyphs = mix(inGlyphs ? 0.3 : 0.0, glyph, near);
    float lit = face ? max(rim, glyphs) : 0.0;
    // A faint halo inside the board, brightest by the tubes.
    float halo = face ? 0.08 * (1.0 - near * 0.5) : 0.0;

    FenrirSurface surface;
    surface.baseColour = material.boardColour.rgb;
    surface.alpha = 1.0;
    surface.normal = n;
    surface.position = vWorldPosition;
    surface.metallic = 0.0;
    surface.roughness = 0.5;
    surface.occlusion = 1.0;
    surface.emissive = vColour.rgb * (lit + halo);
    outColour = FenrirLitColour(surface);
}
