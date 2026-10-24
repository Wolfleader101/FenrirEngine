#version 460
// A user's own material shader (ADR-0024): its own MaterialParams block and texture in set 2, with its own variant key
// FANCY, which the renderer leaves off. Its colour is tint x strength plus its pattern, unlit.

#pragma fenrir default(tint, 0.25, 0.5, 0.75, 1.0)
#pragma fenrir default(strength, 2.0)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 tint;
    float strength;
} material;
#pragma fenrir default(pattern, black)
layout(set = 2, binding = 1) uniform sampler2D pattern;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

void main()
{
#ifdef FANCY
    outColour = vec4(1.0, 0.0, 1.0, 1.0);
#else
    outColour = material.tint * material.strength + texture(pattern, vUv);
#endif
}
