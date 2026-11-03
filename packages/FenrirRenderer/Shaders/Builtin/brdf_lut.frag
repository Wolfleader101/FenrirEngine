#version 460
#extension GL_GOOGLE_include_directive : require
// The BrdfLut pass (ADR-0047): the split sum's BRDF lookup table, made once at start-up. Each texel integrates GGX with
// height-correlated Smith visibility (Heitz 2014, as Filament) over the hemisphere by importance sampling, for N.V
// across (0 to 1, left to right) and perceptual roughness down (0 to 1, top to bottom), into the scale (red) and bias
// (green) the reflected f0 takes: f0 * red + green. Red plus green is what a white metal's single-scattering lobe
// reflects, from which Fenrir/Lighting.glsl compensates for the light it loses (ADR-0048).
#include "Fenrir/Brdf.glsl"

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec2 outScaleBias;

const uint SampleCount = 512u;

void main()
{
    // Offscreen passes put row 0 at the top (NDC +y); texel centres sample the table where its texels are.
    float nDotV = max((vNdc.x + 1.0) * 0.5, 1e-4);
    float roughness = (1.0 - vNdc.y) * 0.5;
    float a = roughness * roughness;
    vec3 v = vec3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);

    vec2 sum = vec2(0.0);
    for (uint i = 0u; i < SampleCount; ++i)
    {
        vec3 h = FenrirSampleGgx(FenrirHammersley(i, SampleCount), a);
        vec3 l = 2.0 * dot(v, h) * h - v;
        float nDotL = l.z;
        float nDotH = h.z;
        float vDotH = max(dot(v, h), 0.0);
        if (nDotL > 0.0)
        {
            // The sample's weight over the GGX pdf D(h) (h.n) / (4 v.h).
            float g = FenrirVisibility(nDotV, nDotL, a) * 4.0 * nDotL * vDotH / max(nDotH, 1e-4);
            float fresnel = pow(1.0 - vDotH, 5.0);
            sum += vec2((1.0 - fresnel) * g, fresnel * g);
        }
    }
    outScaleBias = sum / float(SampleCount);
}
