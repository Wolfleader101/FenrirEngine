#version 460
// AnimationDemo's flat shader, fragment stage: one flat colour, linear.

#pragma fenrir default(colour, 0.2, 0.8, 0.9, 1.0)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 colour;
} material;

layout(location = 0) out vec4 outColour;

void main()
{
    outColour = material.colour;
}
