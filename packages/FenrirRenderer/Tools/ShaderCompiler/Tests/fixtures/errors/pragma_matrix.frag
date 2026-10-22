#version 460
// A default on a matrix member.

layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    mat2 turn;
} material;
#pragma fenrir default(turn, 1.0, 0.0, 0.0, 1.0)
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vec4(material.turn[0], 0.0, 1.0);
}
