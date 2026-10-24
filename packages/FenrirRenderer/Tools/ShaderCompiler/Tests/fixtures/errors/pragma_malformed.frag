#version 460
// A default with its parenthesis left open.

layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 tint;
} material;
#pragma fenrir default(tint, 1.0, 1.0, 1.0, 1.0
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = material.tint;
}
