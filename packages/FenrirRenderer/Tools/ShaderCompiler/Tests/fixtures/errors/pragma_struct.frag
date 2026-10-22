#version 460
// A default on a struct member.

struct Pair
{
    float a;
    float b;
};
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    Pair pair;
} material;
#pragma fenrir default(pair, 1.0, 2.0)
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vec4(material.pair.a);
}
