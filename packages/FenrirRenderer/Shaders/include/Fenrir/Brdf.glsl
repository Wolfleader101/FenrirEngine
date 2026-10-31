// Fenrir/Brdf.glsl: the specular BRDF's terms (ADR-0048), shared by Fenrir/Lighting.glsl and the BrdfLut pass that
// integrates them into the split sum's lookup table, so the table and the direct light agree. It binds nothing. `a` is
// perceptual roughness squared throughout. Core/Brdf is its CPU mirror, which the CPU tests exercise.
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

#endif
