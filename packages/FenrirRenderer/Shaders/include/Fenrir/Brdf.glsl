// Fenrir/Brdf.glsl: the specular BRDF's terms (ADR-0048), shared by Fenrir/Lighting.glsl and the BrdfLut and
// SkySpecular passes that integrate them, so the lookup table, the prefiltered cube and the direct light agree. It binds
// nothing. `a` is perceptual roughness squared throughout. Core/Brdf is its CPU mirror, which the CPU tests exercise.
#ifndef FENRIR_BRDF_GLSL
#define FENRIR_BRDF_GLSL

const float FenrirPi = 3.14159265;

// Trowbridge-Reitz (GGX) normal distribution.
float FenrirDistribution(float nDotH, float a)
{
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (FenrirPi * d * d);
}

// Height-correlated Smith visibility (Heitz 2014, as Filament): the masking-shadowing term G divided by 4 N.V N.L, so a
// specular lobe is D x V x F.
float FenrirVisibility(float nDotV, float nDotL, float a)
{
    float a2 = a * a;
    float lambdaV = nDotL * sqrt((nDotV - nDotV * a2) * nDotV + a2);
    float lambdaL = nDotV * sqrt((nDotL - nDotL * a2) * nDotL + a2);
    return 0.5 / (lambdaV + lambdaL);
}

// The i-th of `count` points of the Hammersley set in [0, 1)^2.
vec2 FenrirHammersley(uint i, uint count)
{
    return vec2(float(i) / float(count), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

// A half vector about +Z sampled from GGX's distribution of D(h) (h.n).
vec3 FenrirSampleGgx(vec2 xi, float a)
{
    float phi = 2.0 * FenrirPi * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

#endif
