#version 460
#extension GL_GOOGLE_include_directive : require
// LightingDemo's forest floor, fragment stage: three ground layers blended and lit through Fenrir/Lighting.glsl. Soil
// is the clearing's, litter the forest's (where the vertex colour's red, ForestWeight, rises) and the path's dirt over
// both (green, PathWeight). Each layer has a base colour texture times a tint and a GL normal map, and the litter and
// the path an ARM map (glTF's occlusion, roughness and metal in red, green and blue; the metal is ignored, the ground
// is never metal, and the default white map reads as rough), repeated every `tiles` metres (x soil, y litter, z path).
// A set holds eight textures, so the soil, mostly under the grass, has no ARM map: soilRoughness, and no occlusion.
//
// A layer's weight is sharpened by its texture's occlusion, as a height: where the mask is half on, the lit stones and
// leaves of the layer on top show and its crevices give way, so the edges break up rather than fade. Under the trees,
// the sky's light is darkened (canopyShade, by the forest weight) for the crowns that GTAO's short reach cannot see,
// and deeper in (the vertex colour's blue, CanopyDepth) the floor's own colour (floorShade), for the sun the far
// trees cast no shadow of.
#include "Fenrir/Lighting.glsl"

#pragma fenrir default(soilTint, 1.0, 1.0, 1.0, 1.0)
#pragma fenrir default(litterTint, 1.0, 1.0, 1.0, 1.0)
#pragma fenrir default(pathTint, 1.0, 1.0, 1.0, 1.0)
#pragma fenrir default(tiles, 3.0, 2.5, 2.0, 0.0)
#pragma fenrir default(canopyShade, 0.45)
#pragma fenrir default(soilRoughness, 0.9)
#pragma fenrir default(floorShade, 0.3)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA, times each layer's base colour texture.
    vec4 soilTint;
    vec4 litterTint;
    vec4 pathTint;
    // Metres per repeat of the soil's, the litter's and the path's textures; w unused.
    vec4 tiles;
    // How much of the sky's light reaches the floor deep under the trees, 0 to 1.
    float canopyShade;
    float soilRoughness;
    // How bright the floor's own colour is deep under the trees, 0 to 1.
    float floorShade;
} material;
#pragma fenrir default(soilColour, white)
layout(set = 2, binding = 1) uniform sampler2D soilColour;
#pragma fenrir default(soilNormal, normal)
layout(set = 2, binding = 2) uniform sampler2D soilNormal;
#pragma fenrir default(litterColour, white)
layout(set = 2, binding = 3) uniform sampler2D litterColour;
#pragma fenrir default(litterNormal, normal)
layout(set = 2, binding = 4) uniform sampler2D litterNormal;
#pragma fenrir default(litterArm, white)
layout(set = 2, binding = 5) uniform sampler2D litterArm;
#pragma fenrir default(pathColour, white)
layout(set = 2, binding = 6) uniform sampler2D pathColour;
#pragma fenrir default(pathNormal, normal)
layout(set = 2, binding = 7) uniform sampler2D pathNormal;
#pragma fenrir default(pathArm, white)
layout(set = 2, binding = 8) uniform sampler2D pathArm;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColour;
layout(location = 3) in vec2 vUv;
layout(location = 4) in vec4 vTangent;
layout(location = 0) out vec4 outColour;

// One layer's samples at this pixel.
struct Layer
{
    vec3 colour;
    // Tangent-space normal, -1 to 1.
    vec3 normal;
    // Occlusion, roughness and metal (unused).
    vec3 arm;
};

Layer Sample(sampler2D colour, sampler2D normal, vec4 tint, float tile)
{
    vec2 uv = vUv / tile;
    Layer layer;
    layer.colour = texture(colour, uv).rgb * tint.rgb;
    layer.normal = texture(normal, uv).xyz * 2.0 - 1.0;
    return layer;
}

Layer Sample(sampler2D colour, sampler2D normal, sampler2D arm, vec4 tint, float tile)
{
    Layer layer = Sample(colour, normal, tint, tile);
    layer.arm = texture(arm, vUv / tile).rgb;
    return layer;
}

Layer Mixed(Layer under, Layer over, float t)
{
    Layer layer;
    layer.colour = mix(under.colour, over.colour, t);
    layer.normal = mix(under.normal, over.normal, t);
    layer.arm = mix(under.arm, over.arm, t);
    return layer;
}

// How much of a layer whose mask is `mask` shows, given its texture's occlusion `height`: the mask's soft edge,
// sharpened and broken up by the texture.
float Coverage(float mask, float height)
{
    return smoothstep(0.35, 0.65, mask + (height - 0.5) * 0.6 * (1.0 - abs(mask * 2.0 - 1.0)));
}

void main()
{
    Layer soil = Sample(soilColour, soilNormal, material.soilTint, material.tiles.x);
    soil.arm = vec3(1.0, material.soilRoughness, 0.0);
    Layer litter = Sample(litterColour, litterNormal, litterArm, material.litterTint, material.tiles.y);
    Layer path = Sample(pathColour, pathNormal, pathArm, material.pathTint, material.tiles.z);
    Layer ground = Mixed(soil, litter, Coverage(vColour.r, litter.arm.r));
    ground = Mixed(ground, path, Coverage(vColour.g, path.arm.r));

    // The mesh's tangent frame: its tangent Gram-Schmidt'd against the normal, the bitangent cross(n, t) by its sign.
    vec3 n = normalize(vNormal);
    vec3 t = normalize(vTangent.xyz - n * dot(n, vTangent.xyz));
    mat3 frame = mat3(t, cross(n, t) * (vTangent.w < 0.0 ? -1.0 : 1.0), n);

    FenrirSurface surface;
    surface.baseColour = ground.colour * mix(1.0, material.floorShade, vColour.b);
    surface.alpha = 1.0;
    surface.normal = normalize(frame * ground.normal);
    surface.position = vWorldPosition;
    surface.metallic = 0.0;
    surface.roughness = ground.arm.g;
    surface.occlusion = ground.arm.r * mix(1.0, material.canopyShade, vColour.r);
    surface.emissive = vec3(0.0);
    outColour = FenrirLitColour(surface);
}
