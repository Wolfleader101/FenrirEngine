#version 460
#extension GL_GOOGLE_include_directive : require
// LightingDemo's grass, fragment stage: lit through Fenrir/Lighting.glsl as builtin:lit is, so the blades take the sun,
// its cascaded shadows, the sky's light and GTAO like everything else in the forest clearing.
//
// A blade's colour runs from rootColour at its foot to tipColour at its tip (UV0's v, 0 to 1), times its vertex colour
// (each blade's own tint) and the instance's (each patch's). The foot is darker to the sky's light too, standing among
// its neighbours. The grass is double-sided: a back face turns its normal round across the ground but keeps it leaning
// up, so both faces of a blade light alike from the sky instead of one facing the ground.
#include "Fenrir/Lighting.glsl"

#pragma fenrir default(rootColour, 0.05, 0.09, 0.02, 1.0)
#pragma fenrir default(tipColour, 0.32, 0.38, 0.1, 1.0)
#pragma fenrir default(roughness, 0.55)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA.
    vec4 rootColour;
    vec4 tipColour;
    float roughness;
} material;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColour;
layout(location = 3) in vec2 vUv;
layout(location = 0) out vec4 outColour;

void main()
{
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing)
    {
        n.xz = -n.xz;
    }

    FenrirSurface surface;
    surface.baseColour = mix(material.rootColour.rgb, material.tipColour.rgb, vUv.y) * vColour.rgb;
    surface.alpha = 1.0;
    surface.normal = n;
    surface.position = vWorldPosition;
    surface.metallic = 0.0;
    surface.roughness = material.roughness;
    surface.occlusion = mix(0.3, 1.0, vUv.y);
    surface.emissive = vec3(0.0);
    outColour = FenrirLitColour(surface);
}
