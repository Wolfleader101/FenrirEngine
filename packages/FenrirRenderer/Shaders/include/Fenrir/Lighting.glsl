// Fenrir/Lighting.glsl: the lighting every material shares (ADR-0048). A shader fills a FenrirSurface its own way and
// calls FenrirLitColour, which returns the surface's lit colour as linear HDR: the frame's directional light, and the
// scene's Sky as image-based lighting (ADR-0047), diffuse from its spherical harmonics and specular from its prefiltered
// cube through the split sum's BRDF lookup table. Intensities are plain multipliers, so the BRDF is scaled by pi: a
// white light of intensity 1 falling straight onto a white rough face gives about 1, not 1 / pi, and so does a white sky
// of intensity 1 all round it.
//
// The shader still owns main, its inputs and its outputs; this is not a surface-function model.
#ifndef FENRIR_LIGHTING_GLSL
#define FENRIR_LIGHTING_GLSL

#include "Fenrir/Frame.glsl"
#include "Fenrir/SphericalHarmonics.glsl"
#include "Fenrir/View.glsl"

// What a pixel of a surface is, for FenrirLitColour.
struct FenrirSurface
{
    // Linear RGB.
    vec3 baseColour;
    // Coverage: FenrirLitColour returns it as its alpha.
    float alpha;
    // The world-space shading normal, unit length, facing the viewer's side of the surface.
    vec3 normal;
    // Where the pixel is, in world space.
    vec3 position;
    // 0 for a dielectric, 1 for a metal.
    float metallic;
    // Perceptual roughness, 0 (a mirror) to 1; clamped to at least 0.04, which keeps highlights finite.
    float roughness;
    // How much of the sky reaches the pixel, 0 to 1: it darkens the sky's light, never the directional light's.
    float occlusion;
    // Linear RGB added to the lit colour.
    vec3 emissive;
};

const float FenrirPi = 3.14159265;

// Trowbridge-Reitz (GGX) normal distribution, with a = roughness squared.
float FenrirDistribution(float nDotH, float a)
{
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (FenrirPi * d * d);
}

// Smith's geometry term with Schlick-GGX, k for direct light.
float FenrirGeometry(float nDotV, float nDotL, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return (nDotV / (nDotV * (1.0 - k) + k)) * (nDotL / (nDotL * (1.0 - k) + k));
}

// The irradiance divided by pi of the sky the scene is lit by at the world normal `normal`: its spherical harmonics
// evaluated in the cube's frame, unscaled by its intensity.
vec3 FenrirSkyIrradiance(vec3 normal)
{
    float basis[9] = FenrirShBasis(FenrirSkyLightDirection(normal));
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 9; ++i)
    {
        sum += texelFetch(fenrirSkyIrradiance, ivec2(i, 0), 0).rgb * basis[i];
    }
    return max(sum, vec3(0.0));
}

// The lit colour of `surface` seen from the view's camera, linear HDR, with its alpha.
vec4 FenrirLitColour(FenrirSurface surface)
{
    vec3 n = surface.normal;
    vec3 v = normalize(view.cameraPosition.xyz - surface.position);
    float nDotV = max(dot(n, v), 1e-4);
    float roughness = clamp(surface.roughness, 0.04, 1.0);
    float metallic = clamp(surface.metallic, 0.0, 1.0);
    vec3 base = surface.baseColour;
    vec3 f0 = mix(vec3(0.04), base, metallic);

    // The directional light. lightDirection is the way the light travels; l points back at it.
    vec3 l = -frame.lightDirection.xyz;
    vec3 h = normalize(v + l);
    float nDotL = max(dot(n, l), 0.0);
    float nDotH = max(dot(n, h), 0.0);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    float d = FenrirDistribution(nDotH, roughness * roughness);
    float g = FenrirGeometry(nDotV, nDotL, roughness);
    vec3 specular = FenrirPi * d * g * fresnel / (4.0 * nDotV * max(nDotL, 1e-4));
    vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base;
    vec3 colour = (diffuse + specular) * frame.lightColour.rgb * nDotL;

    // The sky. The split sum: the prefiltered cube along the reflection at the mip of this roughness, times f0 scaled
    // and biased by the lookup table. What the specular reflects, the diffuse does not.
    vec2 brdf = texture(fenrirBrdfLut, vec2(nDotV, roughness)).rg;
    vec3 skySpecularColour = f0 * brdf.x + brdf.y;
    vec3 r = reflect(-v, n);
    vec3 skySpecular = textureLod(fenrirSkySpecular, FenrirSkyLightDirection(r), roughness * frame.skyLight.w).rgb;
    vec3 skyDiffuse = FenrirSkyIrradiance(n) * (1.0 - skySpecularColour) * (1.0 - metallic) * base;
    // Specular occlusion from the ambient occlusion (Lagarde and de Rousiers 2014, with a = roughness squared as they
    // and Filament use it): rough surfaces take all of it, smooth ones less at grazing angles.
    float occlusion = clamp(surface.occlusion, 0.0, 1.0);
    float a = roughness * roughness;
    float specularOcclusion = clamp(pow(nDotV + occlusion, exp2(-16.0 * a - 1.0)) - 1.0 + occlusion, 0.0, 1.0);
    colour += (skyDiffuse * occlusion + skySpecular * skySpecularColour * specularOcclusion) * frame.skyLight.x;

    colour += surface.emissive;
    return vec4(colour, surface.alpha);
}

#endif
