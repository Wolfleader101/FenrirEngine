#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:debug-triangles, the vertex stage (ADR-0025): solid debug shapes, three vertices a triangle in world space,
// each with its colour as sRGB-encoded RGBA8.
#include "Fenrir/View.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColour;

layout(location = 0) out vec4 vColour;

void main()
{
    vec3 linear = mix(inColour.rgb / 12.92, pow((inColour.rgb + 0.055) / 1.055, vec3(2.4)),
                      greaterThan(inColour.rgb, vec3(0.04045)));
    vColour = vec4(linear, inColour.a);
    gl_Position = view.viewProjection * vec4(inPosition, 1.0);
}
