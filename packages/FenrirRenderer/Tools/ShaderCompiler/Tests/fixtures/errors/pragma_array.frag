#version 460
// A default on an array member.

layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    float weights[2];
} material;
#pragma fenrir default(weights, 1.0, 2.0)
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vec4(material.weights[0]);
}
