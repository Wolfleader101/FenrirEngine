#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:debug-text, the vertex stage (ADR-0025): debug text's glyph quads, laid out by the renderer in the target's
// local coordinates (top-left origin, Y down), with the ImGui vertex layout: position, atlas UV and an sRGB-encoded
// RGBA8 colour. Set 0 is the target (`Fenrir/2D.glsl`): a view's viewport for world text, the window for screen text.
#include "Fenrir/2D.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColour;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColour;

void main()
{
    vec3 linear = mix(inColour.rgb / 12.92, pow((inColour.rgb + 0.055) / 1.055, vec3(2.4)),
                      greaterThan(inColour.rgb, vec3(0.04045)));
    vColour = vec4(linear, inColour.a);
    vUv = inUv;
    gl_Position = target2D.localToClip * vec4(inPosition, 0.0, 1.0);
}
