#version 460
// A 2D shader's parameters: its MaterialParams block and texture in set 1.

#pragma fenrir default(tint, 1.0, 1.0, 1.0, 0.5)
layout(set = 1, binding = 0, std140) uniform MaterialParams
{
    vec4 tint;
} material;
layout(set = 1, binding = 1) uniform sampler2D picture;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = material.tint * texture(picture, vUv);
}
