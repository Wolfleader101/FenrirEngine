#version 460
// A uniform outside a block.

layout(set = 2, binding = 0) uniform float strength;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vec4(strength);
}
