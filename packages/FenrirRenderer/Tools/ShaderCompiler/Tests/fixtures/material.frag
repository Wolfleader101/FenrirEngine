#version 460
// A material set with every std140 layout case the tests check: a vec3 followed by a float that packs into its last
// four bytes, an array whose 16-byte stride pushes the next member out, and a matrix. Expected offsets (std140):
//   baseColour 0, tint 16, strength 28, weights 32 (3 x 16), after 80, transform 96 (64), scale 160; block 176.

#pragma fenrir default(baseColour, 1.0, 0.5, 0.25, 1.0)
#pragma fenrir default(tint, 0.1, 0.2, 0.3)
#pragma fenrir default(strength, 0.75)
#pragma fenrir default(after, 2.5)
#pragma fenrir default(scale, 3, 4.0f)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 baseColour;
    vec3 tint;
    float strength;
    float weights[3];
    float after;
    mat4 transform;
    vec2 scale;
} material;

layout(set = 2, binding = 1) uniform sampler2D albedo;
#pragma fenrir default(normals, normal)
layout(set = 2, binding = 2) uniform sampler2D normals;
#pragma fenrir default(mask, black) // a comment after a pragma is fine
layout(set = 2, binding = 3) uniform sampler2D mask;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

void main()
{
    vec4 sampled = texture(albedo, vUv) * texture(normals, vUv) * texture(mask, vUv);
    outColour = material.transform * (material.baseColour * sampled) + vec4(material.tint, material.strength) +
                vec4(material.weights[0], material.weights[1], material.weights[2], material.after) +
                vec4(material.scale, 0.0, 0.0);
}
