#version 460
// The colour in a uniform block at set 1, binding 0, which the GL copy flattens to uniform buffer binding 4.

layout(set = 1, binding = 0) uniform Fill
{
    vec4 colour;
} fill;

layout(location = 0) out vec4 outColour;

void main()
{
    outColour = fill.colour;
}
