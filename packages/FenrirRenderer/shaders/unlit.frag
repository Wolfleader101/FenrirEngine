#version 460
#extension GL_GOOGLE_include_directive : require

layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 baseColour;
} material;
layout(set = 2, binding = 1) uniform sampler2D baseColourTexture;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vColour;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = material.baseColour * texture(baseColourTexture, vUv) * vColour;
}
