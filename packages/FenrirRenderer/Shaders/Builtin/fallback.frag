#version 460
// The fallback material's program (ADR-0023): flat magenta, unlit, so a mesh with an empty or stale material shows on
// screen. It has no material parameters.

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColour;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vec4(1.0, 0.0, 1.0, 1.0);
}
