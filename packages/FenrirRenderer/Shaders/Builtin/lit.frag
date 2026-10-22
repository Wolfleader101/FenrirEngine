#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:lit (ADR-0024): metallic-roughness with GGX specular, lit by the frame's one directional light and constant
// ambient, written as linear HDR. Intensities are plain multipliers, so the BRDF is scaled by pi: a white light of
// intensity 1 falling straight onto a white rough face gives about 1, not 1 / pi. Its textures arrive with renderer
// ticket 13.
#include "Fenrir/Frame.glsl"
#include "Fenrir/View.glsl"

#pragma fenrir default(baseColour, 1.0, 1.0, 1.0, 1.0)
#pragma fenrir default(emissive, 0.0, 0.0, 0.0, 1.0)
#pragma fenrir default(metallic, 0.0)
#pragma fenrir default(roughness, 0.5)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA.
    vec4 baseColour;
    // Linear RGB added to the lit colour; alpha unused.
    vec4 emissive;
    float metallic;
    float roughness;
} material;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColour;
layout(location = 0) out vec4 outColour;

const float Pi = 3.14159265;

// Trowbridge-Reitz (GGX) normal distribution, with a = roughness squared.
float Distribution(float nDotH, float a)
{
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (Pi * d * d);
}

// Smith's geometry term with Schlick-GGX, k for direct light.
float Geometry(float nDotV, float nDotL, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return (nDotV / (nDotV * (1.0 - k) + k)) * (nDotL / (nDotL * (1.0 - k) + k));
}

void main()
{
    vec4 base = material.baseColour * vColour;
    vec3 n = normalize(vNormal);
    vec3 v = normalize(view.cameraPosition.xyz - vWorldPosition);
    // lightDirection is the way the light travels; l points back at it.
    vec3 l = -frame.lightDirection.xyz;
    vec3 h = normalize(v + l);
    float nDotL = max(dot(n, l), 0.0);
    float nDotV = max(dot(n, v), 1e-4);
    float nDotH = max(dot(n, h), 0.0);
    float roughness = clamp(material.roughness, 0.04, 1.0);
    float metallic = clamp(material.metallic, 0.0, 1.0);

    vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    float d = Distribution(nDotH, roughness * roughness);
    float g = Geometry(nDotV, nDotL, roughness);
    vec3 specular = Pi * d * g * fresnel / (4.0 * nDotV * max(nDotL, 1e-4));
    vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base.rgb;

    vec3 colour = (diffuse + specular) * frame.lightColour.rgb * nDotL;
    colour += frame.ambient.rgb * base.rgb;
    colour += material.emissive.rgb;
    outColour = vec4(colour, base.a);
}
