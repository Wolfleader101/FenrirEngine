#version 460
// The fragment stage's default for strength, which pragma_disagree.vert contradicts.

layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    float strength;
} material;
#pragma fenrir default(strength, 2.0)
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vec4(material.strength);
}
