#version 460
#extension GL_GOOGLE_include_directive : require
// RendererDemo's stripes shader: diagonal stripes of two colours scrolling with the frame's time, shaded by the
// directional light with a simple wrap-around term, plus the scene's Sky's diffuse light from Fenrir/Lighting.glsl.
// Lighting is the shader's own choice (ADR-0024, ADR-0048), and its parameters are its own MaterialParams block in set 2.
#include "Fenrir/Lighting.glsl"

#pragma fenrir default(first, 0.9, 0.75, 0.1, 1.0)
#pragma fenrir default(second, 0.05, 0.05, 0.05, 1.0)
#pragma fenrir default(stripes, 6.0)
#pragma fenrir default(speed, 0.5)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA of the two stripe colours.
    vec4 first;
    vec4 second;
    // Stripes per unit of object space, and how fast they scroll, in stripes per second.
    float stripes;
    float speed;
} material;

layout(location = 0) in vec3 vLocal;
layout(location = 1) in vec3 vNormal;
layout(location = 0) out vec4 outColour;

void main()
{
    float phase = (vLocal.x + vLocal.y + vLocal.z) * material.stripes - frame.time.x * material.speed;
    vec4 base = fract(phase) < 0.5 ? material.first : material.second;
    vec3 n = normalize(vNormal);
    float wrap = dot(n, -frame.lightDirection.xyz) * 0.5 + 0.5;
    vec3 sky = FenrirSkyIrradiance(n) * frame.sky.x;
    outColour = vec4(base.rgb * (frame.lightColour.rgb * wrap + sky), 1.0);
}
