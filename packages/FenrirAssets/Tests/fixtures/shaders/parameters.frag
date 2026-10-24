#version 460
// FenrirAssets's parameter fixture: a parameter of every type, with and without defaults.

#pragma fenrir default(colour, 1.0, 0.5, 0.25, 1.0)
#pragma fenrir default(roughness, 0.5)
#pragma fenrir default(offset, 2.0, 3.0)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 colour;
    float roughness;
    vec2 offset;
    vec3 emissive;
} material;
layout(set = 2, binding = 1) uniform sampler2D colourTexture;
#pragma fenrir default(normalTexture, normal)
layout(set = 2, binding = 2) uniform sampler2D normalTexture;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

void main()
{
    vec4 sampled = texture(colourTexture, vUv) * texture(normalTexture, vUv);
    outColour = material.colour * sampled + vec4(material.emissive, material.roughness) + vec4(material.offset, 0.0, 0.0);
}
