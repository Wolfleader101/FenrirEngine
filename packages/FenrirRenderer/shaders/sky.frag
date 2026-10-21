#version 460
#extension GL_GOOGLE_include_directive : require
#include "Fenrir/View.glsl"

layout(set = 2, binding = 0) uniform samplerCube skyTexture;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

void main()
{
    // A point on the near plane (depth 1 under reversed-Z), back to world space.
    vec4 nearPoint = view.inverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 direction = normalize(nearPoint.xyz / nearPoint.w - view.cameraPosition.xyz);
    outColour = vec4(texture(skyTexture, direction).rgb, 1.0);
}
