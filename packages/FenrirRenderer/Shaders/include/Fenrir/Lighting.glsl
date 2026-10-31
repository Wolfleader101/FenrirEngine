// Fenrir/Lighting.glsl: the lighting every material shares (ADR-0048). A shader fills a FenrirSurface its own way and
// calls FenrirLitColour, which returns the surface's lit colour as linear HDR: the frame's directional light, and the
// scene's Sky as image-based lighting (ADR-0047), diffuse from its spherical harmonics and specular from its prefiltered
// cube through the split sum's BRDF lookup table. Intensities are plain multipliers, so the BRDF is scaled by pi: a
// white light of intensity 1 falling straight onto a white rough face gives about 1, not 1 / pi, and so does a white sky
// of intensity 1 all round it.
//
// The specular lobe is GGX with height-correlated Smith visibility (Fenrir/Brdf.glsl), with multi-scatter energy
// compensation (Fdez-Aguera 2019) read from the lookup table, so a rough metal keeps the light it is lit by, and
// specular anti-aliasing (Tokuyoshi and Kaplanyan 2019), which widens the roughness by how much the normal turns
// across the pixel. That takes the normal's screen-space derivatives, so FenrirLitColour must be called where they are
// defined: not inside a branch that neighbouring pixels may take differently.
//
// The shader still owns main, its inputs and its outputs; this is not a surface-function model.
#ifndef FENRIR_LIGHTING_GLSL
#define FENRIR_LIGHTING_GLSL

#include "Fenrir/Brdf.glsl"
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
    // Perceptual roughness, 0 (a mirror) to 1; clamped to at least 0.04, which keeps highlights finite, and widened
    // where the normal turns quickly across the pixel.
    float roughness;
    // How much of the sky reaches the pixel, 0 to 1: it darkens the sky's light, never the directional light's.
    float occlusion;
    // Linear RGB added to the lit colour.
    vec3 emissive;
};

// Specular anti-aliasing (Tokuyoshi and Kaplanyan 2019, with Filament's constants): perceptual `roughness` widened by
// the variance of `normal` across the pixel's quad, so a highlight thinner than a pixel spreads over it rather than
// flickering in and out as the surface or the camera moves. It takes the shading normal, normal map included, as the
// paper does, so a bumpy map's highlights are filtered too (Filament takes the geometric normal).
const float FenrirAntiAliasingVariance = 0.15;
const float FenrirAntiAliasingThreshold = 0.2;
float FenrirAntiAliasedRoughness(vec3 normal, float roughness)
{
    vec3 dNdx = dFdx(normal);
    vec3 dNdy = dFdy(normal);
    float variance = FenrirAntiAliasingVariance * (dot(dNdx, dNdx) + dot(dNdy, dNdy));
    float kernel = min(2.0 * variance, FenrirAntiAliasingThreshold);
    float a = roughness * roughness;
    return sqrt(sqrt(clamp(a * a + kernel, 0.0, 1.0)));
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
    float roughness = FenrirAntiAliasedRoughness(n, clamp(surface.roughness, 0.04, 1.0));
    float a = roughness * roughness;
    float metallic = clamp(surface.metallic, 0.0, 1.0);
    vec3 base = surface.baseColour;
    vec3 f0 = mix(vec3(0.04), base, metallic);

    // The split sum's scale and bias for f0. Their sum is what a white metal's single-scattering lobe reflects (Ess);
    // the rest it loses to its own microfacets, and multi-scatter compensation gives back.
    vec2 brdf = texture(fenrirBrdfLut, vec2(nDotV, roughness)).rg;
    float singleScatter = max(brdf.x + brdf.y, 1e-4);

    // The directional light. lightDirection is the way the light travels; l points back at it. Its specular lobe is
    // compensated in Filament's form of Fdez-Aguera's: times 1 + f0 (1 / Ess - 1).
    vec3 l = -frame.lightDirection.xyz;
    vec3 h = normalize(v + l);
    float nDotL = max(dot(n, l), 0.0);
    float nDotH = max(dot(n, h), 0.0);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    vec3 energyCompensation = 1.0 + f0 * (1.0 / singleScatter - 1.0);
    vec3 specular = FenrirPi * FenrirDistribution(nDotH, a) * FenrirVisibility(nDotV, max(nDotL, 1e-4), a) * fresnel *
                    energyCompensation;
    vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base;
    vec3 colour = (diffuse + specular) * frame.lightColour.rgb * nDotL;

    // The sky (Fdez-Aguera 2019). The single-scattering share reflects the prefiltered cube along the reflection at the
    // mip of this roughness: f0 scaled and biased by the table. What it loses scatters on, each bounce reflecting the
    // average Fresnel, and leaves spread wide enough to take the sky's irradiance. The diffuse takes what neither does.
    vec3 skySingleScatter = f0 * brdf.x + brdf.y;
    float lost = 1.0 - singleScatter;
    // The hemispherical average of Schlick's Fresnel.
    vec3 averageFresnel = f0 + (1.0 - f0) / 21.0;
    vec3 skyMultiScatter = skySingleScatter * averageFresnel / (1.0 - lost * averageFresnel) * lost;
    vec3 r = reflect(-v, n);
    vec3 skySpecular = textureLod(fenrirSkySpecular, FenrirSkyLightDirection(r), roughness * frame.skyLight.w).rgb;
    vec3 skyIrradiance = FenrirSkyIrradiance(n);
    vec3 skyDiffuse = (1.0 - skySingleScatter - skyMultiScatter) * (1.0 - metallic) * base;
    // Specular occlusion from the ambient occlusion (Lagarde and de Rousiers 2014, with a = roughness squared as they
    // and Filament use it): rough surfaces take all of it, smooth ones less at grazing angles. The multiple-scattering
    // share is diffuse-like, so it takes the ambient occlusion.
    float occlusion = clamp(surface.occlusion, 0.0, 1.0);
    float specularOcclusion = clamp(pow(nDotV + occlusion, exp2(-16.0 * a - 1.0)) - 1.0 + occlusion, 0.0, 1.0);
    colour += (skyIrradiance * (skyDiffuse + skyMultiScatter) * occlusion +
               skySpecular * skySingleScatter * specularOcclusion) *
              frame.skyLight.x;

    colour += surface.emissive;
    return vec4(colour, surface.alpha);
}

#endif
