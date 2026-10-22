#version 460
#extension GL_GOOGLE_include_directive : require
// The vertex stage of FenrirAssets's parameter fixture.
#include "Fenrir/Vertex.glsl"

layout(location = 0) out vec2 vUv;

void main()
{
    vUv = inUv0;
    gl_Position = vec4(inPosition, 1.0);
}
