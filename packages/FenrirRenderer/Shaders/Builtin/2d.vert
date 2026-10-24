#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:2d, the vertex stage (ADR-0027): a 2D draw list's vertices in the ImGui layout (position, UV, an RGBA8
// colour), in the target's local coordinates (top-left origin, Y down). Set 0 is the 2D target (`Fenrir/2D.glsl`).
//
// The renderer picks the variant from the 2D pipeline description: INPUT_SRGB for sRGB-encoded vertex colours, and
// GAMMA for the gamma compatibility path, where the shader works in sRGB-encoded values and the target does not encode.
#include "Fenrir/2D.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColour;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColour;

vec3 Decode(vec3 srgb)
{
    return mix(srgb / 12.92, pow((srgb + 0.055) / 1.055, vec3(2.4)), greaterThan(srgb, vec3(0.04045)));
}

vec3 Encode(vec3 linear)
{
    return mix(linear * 12.92, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055, greaterThan(linear, vec3(0.0031308)));
}

void main()
{
    vec3 colour = inColour.rgb;
#if defined(GAMMA) && !defined(INPUT_SRGB)
    colour = Encode(colour);
#elif !defined(GAMMA) && defined(INPUT_SRGB)
    colour = Decode(colour);
#endif
    vColour = vec4(colour, inColour.a);
    vUv = inUv;
    gl_Position = target2D.localToClip * vec4(inPosition, 0.0, 1.0);
}
