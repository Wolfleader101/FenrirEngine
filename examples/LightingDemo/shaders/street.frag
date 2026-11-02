#version 460
#extension GL_GOOGLE_include_directive : require
// LightingDemo's wet streets, fragment stage: asphalt after rain, lit through Fenrir/Lighting.glsl. The asphalt's base
// colour, GL normal map and ARM map (glTF's occlusion, roughness and metal; the metal ignored) repeat every `tile`
// metres across the ground, read by world position (u along +X, v along -Z, so the map's tangent is +X and its
// bitangent -Z).
//
// The puddle mask is made here from the world position: two octaves of value noise, cut at `puddles`, the share of the
// street under water. In a puddle the surface is the water's: smooth (puddleRoughness), level (the normal map gives way
// to straight up) and over darker asphalt, so the neon signs, the lamps and the headlights are mirrored in it through
// the rect lights' LTC specular and the point and spot lights' GGX. Between the puddles the asphalt is damp: its own
// roughness times `damp`, darker a little. At the puddles' edges the water thins over the stones, so the stones' high
// points (the ARM map's occlusion, as a height) break the surface first.
#include "Fenrir/Lighting.glsl"

#pragma fenrir default(tint, 1.0, 1.0, 1.0, 1.0)
#pragma fenrir default(tile, 3.0)
#pragma fenrir default(puddles, 0.5)
#pragma fenrir default(puddleRoughness, 0.03)
#pragma fenrir default(damp, 0.3)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA, times the base colour texture.
    vec4 tint;
    // Metres per repeat of the asphalt's textures.
    float tile;
    // The share of the street under water, 0 (dry, and no damp either) to 1.
    float puddles;
    float puddleRoughness;
    // The damp asphalt's roughness as a share of its own, 0 to 1.
    float damp;
} material;
#pragma fenrir default(asphaltColour, white)
layout(set = 2, binding = 1) uniform sampler2D asphaltColour;
#pragma fenrir default(asphaltNormal, normal)
layout(set = 2, binding = 2) uniform sampler2D asphaltNormal;
#pragma fenrir default(asphaltArm, white)
layout(set = 2, binding = 3) uniform sampler2D asphaltArm;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 0) out vec4 outColour;

// 0 to 1 at each corner of the integer lattice.
float Lattice(vec2 p)
{
    uvec2 q = uvec2(ivec2(p) + ivec2(1 << 16));
    uint h = q.x * 0x8da6b343u ^ q.y * 0xd8163841u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return float(h) / 4294967295.0;
}

// Smooth value noise, 0 to 1.
float Noise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(Lattice(cell), Lattice(cell + vec2(1.0, 0.0)), f.x),
               mix(Lattice(cell + vec2(0.0, 1.0)), Lattice(cell + vec2(1.0, 1.0)), f.x), f.y);
}

// How wet the ground at `ground` (world XZ) is, the water's depth: above 0.5 a puddle. Puddles a few metres across,
// with ragged edges.
float Water(vec2 ground)
{
    float broad = Noise(ground / 5.0);
    float ragged = Noise(ground / 1.3 + vec2(17.3, 5.1));
    float wet = broad * 0.75 + ragged * 0.25;
    return 0.5 + (wet - (1.0 - material.puddles)) * 2.5;
}

void main()
{
    vec2 uv = vec2(vWorldPosition.x, -vWorldPosition.z) / material.tile;
    vec3 colour = texture(asphaltColour, uv).rgb * material.tint.rgb;
    vec3 tangentNormal = texture(asphaltNormal, uv).xyz * 2.0 - 1.0;
    vec3 arm = texture(asphaltArm, uv).rgb;

    // The stones' tops break through the thin water at a puddle's edge.
    float water = Water(vWorldPosition.xz) - (arm.r - 0.5) * 0.4;
    float puddle = material.puddles > 0.0 ? smoothstep(0.45, 0.55, water) : 0.0;
    float dampness = material.puddles > 0.0 ? 1.0 : 0.0;

    vec3 n = normalize(vNormal);
    vec3 t = normalize(vec3(1.0, 0.0, 0.0) - n * n.x);
    mat3 frame = mat3(t, cross(n, t), n);
    vec3 asphalt = normalize(frame * tangentNormal);

    FenrirSurface surface;
    surface.baseColour = colour * mix(1.0, mix(0.8, 0.55, puddle), dampness);
    surface.alpha = 1.0;
    surface.normal = normalize(mix(asphalt, n, puddle));
    surface.position = vWorldPosition;
    surface.metallic = 0.0;
    surface.roughness = mix(arm.g * mix(1.0, material.damp, dampness), material.puddleRoughness, puddle);
    surface.occlusion = mix(arm.r, 1.0, puddle);
    surface.emissive = vec3(0.0);
    outColour = FenrirLitColour(surface);
}
