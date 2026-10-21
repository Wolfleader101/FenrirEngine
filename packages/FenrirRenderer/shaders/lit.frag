#version 460
#extension GL_GOOGLE_include_directive : require
#include "Fenrir/Frame.glsl"
#include "Fenrir/View.glsl"

// Set 2, the material: reflected by FenrirShaderCompiler, so the renderer finds these by name.
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 baseColour;
    vec4 emissive;
    float metallic;
    float roughness;
} material;
layout(set = 2, binding = 1) uniform sampler2D baseColourTexture;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vColour;
layout(location = 0) out vec4 outColour;

const float PI = 3.14159265;

float DistributionGGX(float NdotH, float a)
{
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
}

void main()
{
    vec4 base = material.baseColour * texture(baseColourTexture, vUv) * vColour;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(view.cameraPosition.xyz - vWorldPosition);
    vec3 L = normalize(-frame.lightDirection.xyz);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float roughness = clamp(material.roughness, 0.04, 1.0);
    float metallic = clamp(material.metallic, 0.0, 1.0);

    vec3 F0 = mix(vec3(0.04), base.rgb, metallic);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    float D = DistributionGGX(NdotH, roughness * roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    vec3 specular = D * G * F / (4.0 * NdotV * max(NdotL, 1e-4));
    vec3 diffuse = (1.0 - F) * (1.0 - metallic) * base.rgb / PI;

    vec3 colour = (diffuse + specular) * frame.lightColour.rgb * NdotL;
    colour += frame.ambient.rgb * base.rgb;
    colour += material.emissive.rgb;
    outColour = vec4(colour, base.a);
}
