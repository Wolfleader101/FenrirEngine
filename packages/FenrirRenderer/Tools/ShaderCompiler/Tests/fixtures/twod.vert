#version 460
#extension GL_GOOGLE_include_directive : require
// A 2D shader: set 0 is the 2D target's, so its material is set 1.
#include "Fenrir/2D.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 0) out vec2 vUv;

void main()
{
    vUv = inUv;
    gl_Position = target2D.localToClip * vec4(inPosition, 0.0, 1.0);
}
