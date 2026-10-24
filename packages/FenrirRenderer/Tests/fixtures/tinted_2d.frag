#version 460
// A 2D shader with a parameter block, for a 2D command's parameter bytes written over its parameter defaults.

layout(set = 1, binding = 0, std140) uniform MaterialParams
{
    vec4 tint;
    float strength;
} params;
#pragma fenrir default(tint, 1.0, 1.0, 1.0, 1.0)
#pragma fenrir default(strength, 0.5)

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColour;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vColour * params.tint * params.strength;
}
