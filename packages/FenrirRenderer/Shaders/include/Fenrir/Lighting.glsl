// Fenrir/Lighting.glsl: the lighting every material shares (ADR-0048). A shader fills a FenrirSurface its own way and
// calls FenrirLitColour, which returns the surface's lit colour as linear HDR: the frame's directional light, its point,
// spot and rect lights, and the scene's Sky as image-based lighting (ADR-0047), diffuse from its spherical harmonics and
// specular from its prefiltered cube through the split sum's BRDF lookup table, darkened by the surface's occlusion and
// the view's ambient occlusion (ADR-0049), which never touch the direct lights. The directional light is shadowed by the
// view's cascades (Fenrir/Shadows.glsl, ADR-0050). Intensities are plain multipliers, so
// the BRDF is scaled by pi: a white light of intensity 1 falling straight onto a white rough face gives about 1, not
// 1 / pi, as does a point light of intensity 1 one unit away (well inside its range), and a white sky of intensity 1
// all round it. A rect light's intensity is its radiance: a white one of intensity 1 filling a white rough face's view
// lights it about 1.
//
// The specular lobe is GGX with height-correlated Smith visibility (Fenrir/Brdf.glsl), with multi-scatter energy
// compensation (Fdez-Aguera 2019) read from the lookup table, so a rough metal keeps the light it is lit by, and
// specular anti-aliasing (Tokuyoshi and Kaplanyan 2019), which widens the roughness by how much the normal turns
// across the pixel. That takes the normal's screen-space derivatives, so FenrirLitColour must be called where they are
// defined: not inside a branch that neighbouring pixels may take differently.
//
// The shadow's biases take the face's own normal from the position's screen-space derivatives too.
//
// The shader still owns main, its inputs and its outputs; this is not a surface-function model.
#ifndef FENRIR_LIGHTING_GLSL
#define FENRIR_LIGHTING_GLSL

#include "Fenrir/Brdf.glsl"
#include "Fenrir/Frame.glsl"
#include "Fenrir/Shadows.glsl"
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

// How a point or spot light of intensity 1 falls off at the squared distance `distanceSquared`, windowed to zero at
// `range` (glTF's KHR_lights_punctual, Karis 2013): 1 / d^2 times (1 - (d / range)^4)^2. d^2 is held at least 1e-4,
// so a pixel on the light stays finite. Core/PunctualLights mirrors it on the CPU.
float FenrirDistanceFalloff(float distanceSquared, float range)
{
    float ratio = distanceSquared / (range * range);
    float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
    return window * window / max(distanceSquared, 1e-4);
}

// How a spot light falls off at the cosine `cosAngle` of the angle off its axis: smoothstep from its outer cone's
// cosine to its inner's, by the record's scale and offset. A point light's scale 0 and offset 1 give 1.
float FenrirConeFalloff(float cosAngle, float coneScale, float coneOffset)
{
    float t = clamp(cosAngle * coneScale + coneOffset, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// What a light of colour 1 arriving along `l` (pointing back at it) reflects towards `v`, N.L included: GGX with
// height-correlated Smith visibility, its specular lobe compensated in Filament's form of Fdez-Aguera's (times
// `energyCompensation`, 1 + f0 (1 / Ess - 1)), and the diffuse what the Fresnel leaves, scaled by pi as the file says.
vec3 FenrirDirectLight(vec3 n, vec3 v, vec3 l, float nDotV, float a, vec3 f0, vec3 energyCompensation, float metallic,
                       vec3 base)
{
    vec3 h = normalize(v + l);
    float nDotL = max(dot(n, l), 0.0);
    float nDotH = max(dot(n, h), 0.0);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    vec3 specular = FenrirPi * FenrirDistribution(nDotH, a) * FenrirVisibility(nDotV, max(nDotL, 1e-4), a) * fresnel *
                    energyCompensation;
    vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base;
    return (diffuse + specular) * nDotL;
}

// Rect lights (ADR-0048), shaded with linearly transformed cosines (Heitz, Dupuy, Hill and Neubelt 2016): the light is
// a cosine lobe's integral over the rect, a plain cosine for the diffuse and, for the specular, a cosine turned into
// the shape of GGX by a transform from fenrirLtcMatrix. Each edge's vector form factor is summed and the horizon
// clipped by Hill's sphere approximation from fenrirLtcAmplitude, so no polygon is clipped. Core/RectLights mirrors it
// on the CPU. The tables are read at mip 0 explicitly: the light loop is not uniform control flow.

// A lookup's [0, 1] coordinates moved onto the 64 x 64 tables' first and last texels' centres.
vec2 FenrirLtcUv(vec2 lookup)
{
    return lookup * (63.0 / 64.0) + 0.5 / 64.0;
}

// The vector form factor of the arc from the unit vector `from` to `to`: their cross product times
// theta / (2 pi sin theta), from Hill's rational fit, accurate to where the two are opposite.
vec3 FenrirLtcEdge(vec3 from, vec3 to)
{
    float x = dot(from, to);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float thetaSinTheta = x > 0.0 ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(from, to) * thetaSinTheta;
}

// The integral over the rect `light` of a cosine lobe about +Z in the frame `toLobe` takes world directions into, seen
// from `position`: its form factor, 0 to 1, clipped to the lobe's upper hemisphere. 0 from behind a one-sided light.
float FenrirLtcIntegral(FenrirLight light, vec3 position, mat3 toLobe)
{
    vec3 centre = light.position - position;
    // Behind the light: on the side it does not face.
    bool behind = dot(centre, light.direction) > 0.0;
    if (behind && light.twoSided == 0u)
    {
        return 0.0;
    }
    vec3 c0 = normalize(toLobe * (centre - light.halfWidth - light.halfHeight));
    vec3 c1 = normalize(toLobe * (centre + light.halfWidth - light.halfHeight));
    vec3 c2 = normalize(toLobe * (centre + light.halfWidth + light.halfHeight));
    vec3 c3 = normalize(toLobe * (centre - light.halfWidth + light.halfHeight));
    vec3 sum = FenrirLtcEdge(c0, c1) + FenrirLtcEdge(c1, c2) + FenrirLtcEdge(c2, c3) + FenrirLtcEdge(c3, c0);
    float formFactor = length(sum);
    // Seen from behind, the corners wind the other way.
    float elevation = (behind ? -sum.z : sum.z) / max(formFactor, 1e-7);
    float horizon = textureLod(fenrirLtcAmplitude, FenrirLtcUv(vec2(elevation * 0.5 + 0.5, formFactor)), 0.0).a;
    return formFactor * horizon;
}

// How the rect `light`'s light is windowed by distance: (1 - (d / range)^4)^2, d the distance from `position` to the
// rect's nearest point; 0 at and past the range. The lobes' integrals already fall with distance.
float FenrirRectWindow(FenrirLight light, vec3 position)
{
    vec3 offset = position - light.position;
    float across = clamp(dot(offset, light.halfWidth) / dot(light.halfWidth, light.halfWidth), -1.0, 1.0);
    float along = clamp(dot(offset, light.halfHeight) / dot(light.halfHeight, light.halfHeight), -1.0, 1.0);
    vec3 gap = offset - light.halfWidth * across - light.halfHeight * along;
    float distanceSquared = dot(gap, gap);
    float rangeSquared = light.range * light.range;
    if (distanceSquared >= rangeSquared)
    {
        return 0.0;
    }
    float ratio = distanceSquared / rangeSquared;
    float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
    return window * window;
}

// World directions into the tangent space of a surface at `n` seen along `v`: x towards the view, z the normal.
mat3 FenrirLtcSurfaceFrame(vec3 n, vec3 v)
{
    vec3 tangent = v - n * dot(n, v);
    if (dot(tangent, tangent) < 1e-8)
    {
        // Seen straight on, the lobe is round about the normal: any tangent will do.
        tangent = cross(n, abs(n.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0));
    }
    tangent = normalize(tangent);
    return transpose(mat3(tangent, cross(n, tangent), n));
}

// What the rect `light` reflects towards the viewer from `position`: the diffuse lobe's integral times `diffuse`, and
// GGX's, through the transform for `nDotV` and perceptual `roughness`, times `specular`, the lobe's directional albedo.
vec3 FenrirRectLight(FenrirLight light, vec3 position, vec3 n, vec3 v, float nDotV, float roughness, vec3 diffuse,
                     vec3 specular)
{
    float window = FenrirRectWindow(light, position);
    if (window <= 0.0)
    {
        return vec3(0.0);
    }
    mat3 toSurface = FenrirLtcSurfaceFrame(n, v);
    vec4 m = textureLod(fenrirLtcMatrix, FenrirLtcUv(vec2(roughness, sqrt(1.0 - nDotV))), 0.0);
    mat3 toLobe = mat3(vec3(m.x, 0.0, m.y), vec3(0.0, 1.0, 0.0), vec3(m.z, 0.0, m.w)) * toSurface;
    return light.colour * window *
           (diffuse * FenrirLtcIntegral(light, position, toSurface) +
            specular * FenrirLtcIntegral(light, position, toLobe));
}

// The view's ambient occlusion (Fenrir/View.glsl) at this fragment's pixel; 1 where the view has none.
float FenrirViewOcclusion()
{
    ivec2 last = textureSize(fenrirAmbientOcclusion, 0) - 1;
    return texelFetch(fenrirAmbientOcclusion, min(ivec2(gl_FragCoord.xy), last), 0).r;
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

    // The directional light, shadowed by the view's cascades. lightDirection is the way the light travels; l points
    // back at it.
    vec3 energyCompensation = 1.0 + f0 * (1.0 / singleScatter - 1.0);
    vec3 sunward = -frame.lightDirection.xyz;
    // The shadow's biases follow the face itself, not a normal map's turn of it: its normal from the position's
    // derivatives, on the viewer's side as the shading normal is.
    vec3 face = cross(dFdx(surface.position), dFdy(surface.position));
    face = dot(face, face) > 1e-20 ? normalize(face) : n;
    face = dot(face, n) < 0.0 ? -face : face;
    int cascade;
    float sunShadow = FenrirSunShadow(surface.position, face, sunward, cascade);
    vec3 colour = FenrirDirectLight(n, v, sunward, nDotV, a, f0, energyCompensation, metallic, base) *
                  frame.lightColour.rgb * sunShadow;

    // What the specular lobe reflects of light from all round, by the split sum: f0 scaled and biased by the table. A
    // rect light's specular takes it, compensated; its diffuse what is left.
    vec3 specularAlbedo = f0 * brdf.x + brdf.y;
    vec3 rectSpecular = specularAlbedo * energyCompensation;
    vec3 rectDiffuse = (1.0 - specularAlbedo) * (1.0 - metallic) * base;

    // The point, spot and rect lights, a plain loop over the frame's light buffer until clustering lands (ADR-0049),
    // skipping each whose range does not reach the pixel.
    for (uint i = 0u; i < frame.lights.x; ++i)
    {
        FenrirLight light = fenrirLights.items[i];
        if (light.kind == FenrirLightRect)
        {
            colour += FenrirRectLight(light, surface.position, n, v, nDotV, roughness, rectDiffuse, rectSpecular);
            continue;
        }
        vec3 toLight = light.position - surface.position;
        float distanceSquared = dot(toLight, toLight);
        if (distanceSquared >= light.range * light.range)
        {
            continue;
        }
        vec3 l = toLight * inversesqrt(max(distanceSquared, 1e-12));
        float falloff = FenrirDistanceFalloff(distanceSquared, light.range) *
                        FenrirConeFalloff(dot(-l, light.direction), light.coneScale, light.coneOffset);
        colour += FenrirDirectLight(n, v, l, nDotV, a, f0, energyCompensation, metallic, base) * light.colour * falloff;
    }

    // The sky (Fdez-Aguera 2019). The single-scattering share reflects the prefiltered cube along the reflection at the
    // mip of this roughness: f0 scaled and biased by the table. What it loses scatters on, each bounce reflecting the
    // average Fresnel, and leaves spread wide enough to take the sky's irradiance. The diffuse takes what neither does.
    vec3 skySingleScatter = specularAlbedo;
    float lost = 1.0 - singleScatter;
    // The hemispherical average of Schlick's Fresnel.
    vec3 averageFresnel = f0 + (1.0 - f0) / 21.0;
    vec3 skyMultiScatter = skySingleScatter * averageFresnel / (1.0 - lost * averageFresnel) * lost;
    vec3 r = reflect(-v, n);
    vec3 skySpecular = textureLod(fenrirSkySpecular, FenrirSkyLightDirection(r), roughness * frame.skyLight.w).rgb;
    vec3 skyIrradiance = FenrirSkyIrradiance(n);
    vec3 skyDiffuse = (1.0 - skySingleScatter - skyMultiScatter) * (1.0 - metallic) * base;
    // The ambient occlusion: the lesser of the surface's own and the view's GTAO at this pixel (Filament's choice, so
    // occlusion a texture bakes and GTAO also finds does not darken twice). Specular occlusion from it (Lagarde and de
    // Rousiers 2014, with a = roughness squared as they and Filament use it): rough surfaces take all of it, smooth
    // ones less at grazing angles. The multiple-scattering share is diffuse-like, so it takes the ambient occlusion.
    float occlusion = min(clamp(surface.occlusion, 0.0, 1.0), FenrirViewOcclusion());
    float specularOcclusion = clamp(pow(nDotV + occlusion, exp2(-16.0 * a - 1.0)) - 1.0 + occlusion, 0.0, 1.0);
    colour += (skyIrradiance * (skyDiffuse + skyMultiScatter) * occlusion +
               skySpecular * skySingleScatter * specularOcclusion) *
              frame.skyLight.x;

    colour += surface.emissive;
    if (view.shadowInfo.y != 0u)
    {
        colour *= FenrirCascadeTint(cascade);
    }
    // SceneColor is RGBA16F: a glint past its largest finite value would be written as infinity, which the bloom
    // chain turns to NaN and spreads over its coarsest levels' texels, a flickering black box.
    return vec4(min(colour, vec3(65504.0)), surface.alpha);
}

#endif
