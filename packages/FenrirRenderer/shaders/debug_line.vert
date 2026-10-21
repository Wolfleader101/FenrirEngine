#version 460
#extension GL_GOOGLE_include_directive : require
#include "Fenrir/View.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec4 inColour;
layout(location = 0) out vec4 vColour;

void main()
{
    vColour = inColour;
    gl_Position = view.viewProjection * vec4(inPosition, 1.0);
}
