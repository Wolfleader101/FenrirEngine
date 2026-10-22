#version 460
#extension GL_GOOGLE_include_directive : require
// A vertex stage passing UV0 on at location 0, paired with fragment fixtures that read it.
#include "Fenrir/Vertex.glsl"

layout(location = 0) out vec2 vUv;

void main()
{
    vUv = inUv0;
    gl_Position = vec4(inPosition, 1.0);
}
