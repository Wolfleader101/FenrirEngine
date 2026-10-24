#version 460
// Two defaults for one member in one stage.

layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    float strength;
} material;
#pragma fenrir default(strength, 1.0)
#pragma fenrir default(strength, 2.0)
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vec4(material.strength);
}
