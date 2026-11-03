#version 460
// A uniform block and nine textures in set 2: a set holds at most 8 bindings of a kind, however they are numbered.

layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 tint;
} material;
layout(set = 2, binding = 1) uniform sampler2D first;
layout(set = 2, binding = 2) uniform sampler2D second;
layout(set = 2, binding = 3) uniform sampler2D third;
layout(set = 2, binding = 4) uniform sampler2D fourth;
layout(set = 2, binding = 5) uniform sampler2D fifth;
layout(set = 2, binding = 6) uniform sampler2D sixth;
layout(set = 2, binding = 7) uniform sampler2D seventh;
layout(set = 2, binding = 8) uniform sampler2D eighth;
layout(set = 2, binding = 10) uniform sampler2D ninth;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = material.tint * texture(first, vec2(0.0)) * texture(second, vec2(0.0)) * texture(third, vec2(0.0)) *
                texture(fourth, vec2(0.0)) * texture(fifth, vec2(0.0)) * texture(sixth, vec2(0.0)) *
                texture(seventh, vec2(0.0)) * texture(eighth, vec2(0.0)) * texture(ninth, vec2(0.0));
}
