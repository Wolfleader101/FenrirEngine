#version 460
// A uniform block and five textures in set 2: a set holds at most 4 bindings of a kind, however they are numbered.

layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 tint;
} material;
layout(set = 2, binding = 1) uniform sampler2D first;
layout(set = 2, binding = 2) uniform sampler2D second;
layout(set = 2, binding = 3) uniform sampler2D third;
layout(set = 2, binding = 4) uniform sampler2D fourth;
layout(set = 2, binding = 6) uniform sampler2D fifth;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = material.tint * texture(first, vec2(0.0)) * texture(second, vec2(0.0)) * texture(third, vec2(0.0)) *
                texture(fourth, vec2(0.0)) * texture(fifth, vec2(0.0));
}
