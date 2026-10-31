// Fenrir/SphericalHarmonics.glsl: the real L2 spherical harmonics basis the sky's diffuse light is projected onto
// (ADR-0047), shared by builtin:sky-irradiance, which projects it, and Fenrir/Lighting.glsl, which evaluates it. The CPU
// mirror is Core/SphericalHarmonics.cpp.
#ifndef FENRIR_SPHERICAL_HARMONICS_GLSL
#define FENRIR_SPHERICAL_HARMONICS_GLSL

// The nine basis functions at the unit direction d, in the order Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22.
float[9] FenrirShBasis(vec3 d)
{
    return float[9](0.282095, 0.488603 * d.y, 0.488603 * d.z, 0.488603 * d.x, 1.092548 * d.x * d.y,
                    1.092548 * d.y * d.z, 0.315392 * (3.0 * d.z * d.z - 1.0), 1.092548 * d.x * d.z,
                    0.546274 * (d.x * d.x - d.y * d.y));
}

#endif
