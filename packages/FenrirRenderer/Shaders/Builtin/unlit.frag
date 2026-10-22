#version 460
// builtin:unlit (ADR-0024): base colour times vertex colour, written as linear HDR. Its textures arrive with renderer
// ticket 13.

#pragma fenrir default(baseColour, 1.0, 1.0, 1.0, 1.0)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA.
    vec4 baseColour;
} material;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColour;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = material.baseColour * vColour;
}
