// Fenrir/SkinMaths.glsl: linear blend skinning's maths (ADR-0035), for any stage: a palette entry's layout, the blend of
// four entries by their weights into one affine M with upper 3x3 A, and the normal and tangent through A. It declares
// no inputs and no buffers: Fenrir/Skinning.glsl applies it to a vertex stage's joints and weights, and builtin:skinning
// to a mesh's streams in compute (ADR-0050).
//
// The renderer's C++ mirrors these functions (src/Core/Skinning.hpp), and the tests hold the two to each other.
#ifndef FENRIR_SKIN_MATHS_GLSL
#define FENRIR_SKIN_MATHS_GLSL

// One palette entry: rows 0 to 2 of an affine skinning matrix (model x inverse bind); its last row is (0, 0, 0, 1).
struct FenrirBone
{
    vec4 r0;
    vec4 r1;
    vec4 r2;
};

// The four entries blended by their weights, which sum to 1 or are all 0. Zero total weight is the identity: the vertex
// rides rigidly at its bind position.
FenrirBone FenrirBlendBones(FenrirBone b0, FenrirBone b1, FenrirBone b2, FenrirBone b3, vec4 weights)
{
    if (weights.x + weights.y + weights.z + weights.w <= 0.0)
    {
        return FenrirBone(vec4(1.0, 0.0, 0.0, 0.0), vec4(0.0, 1.0, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0));
    }
    FenrirBone blended;
    blended.r0 = b0.r0 * weights.x + b1.r0 * weights.y + b2.r0 * weights.z + b3.r0 * weights.w;
    blended.r1 = b0.r1 * weights.x + b1.r1 * weights.y + b2.r1 * weights.z + b3.r1 * weights.w;
    blended.r2 = b0.r2 * weights.x + b1.r2 * weights.y + b2.r2 * weights.z + b3.r2 * weights.w;
    return blended;
}

// M x (p, 1).
vec3 FenrirSkinPoint(FenrirBone m, vec3 position)
{
    vec4 p = vec4(position, 1.0);
    return vec3(dot(m.r0, p), dot(m.r1, p), dot(m.r2, p));
}

// A of the blended rows, as columns.
mat3 FenrirSkinLinear(FenrirBone m)
{
    return mat3(vec3(m.r0.x, m.r1.x, m.r2.x), vec3(m.r0.y, m.r1.y, m.r2.y), vec3(m.r0.z, m.r1.z, m.r2.z));
}

bool FenrirIsFinite(vec3 v)
{
    return !any(isnan(v)) && !any(isinf(v));
}

// The unit vector along v, or fallback when v is too short or not finite.
vec3 FenrirNormaliseOr(vec3 v, vec3 fallback)
{
    float length2 = dot(v, v);
    if (!FenrirIsFinite(v) || isnan(length2) || isinf(length2) || length2 < 1e-12)
    {
        return fallback;
    }
    return v * inversesqrt(length2);
}

// The bind-space normal n through A: normalize(sign(det A) x cof(A) x n), the inverse transpose's direction. When A is
// near-singular (|det A| < 1e-6 c^3, c its longest column) or that is degenerate, normalize(A x n); then n itself.
vec3 FenrirSkinModelNormal(mat3 a, vec3 n)
{
    vec3 c0 = cross(a[1], a[2]);
    vec3 c1 = cross(a[2], a[0]);
    vec3 c2 = cross(a[0], a[1]);
    float det = dot(a[0], c0);
    float c = max(length(a[0]), max(length(a[1]), length(a[2])));
    vec3 bind = FenrirNormaliseOr(n, vec3(0.0, 1.0, 0.0));
    vec3 direct = FenrirNormaliseOr(a * n, bind);
    if (!(abs(det) >= 1e-6 * c * c * c))
    {
        return direct;
    }
    vec3 cofactor = (det < 0.0 ? -1.0 : 1.0) * (mat3(c0, c1, c2) * n);
    return FenrirNormaliseOr(cofactor, direct);
}

// t made unit and perpendicular to the unit normal n by Gram-Schmidt; when that is degenerate, the axis least aligned
// with n made perpendicular to it.
vec3 FenrirPerpendicularTangent(vec3 t, vec3 n)
{
    t -= n * dot(n, t);
    vec3 a = abs(n);
    vec3 axis = a.x <= a.y && a.x <= a.z ? vec3(1.0, 0.0, 0.0) : (a.y <= a.z ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0));
    vec3 perpendicular = normalize(axis - n * dot(n, axis));
    return length(t) < 1e-6 ? perpendicular : FenrirNormaliseOr(t, perpendicular);
}

// -1 when A mirrors, else 1.
float FenrirSkinSign(mat3 a)
{
    return determinant(a) < 0.0 ? -1.0 : 1.0;
}

#endif
