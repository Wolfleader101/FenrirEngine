// Fenrir/Skinning.glsl: linear blend skinning for vertex stages (ADR-0035), under the SKINNED variant key. It declares
// the palette (set 3, binding 1) and the joints and weights at vertex locations 6 and 7, and gives
// FenrirSkinPosition, FenrirSkinNormal and FenrirSkinTangent, each taking the current draw's record and returning world
// space. They blend the four palette entries the vertex names by its weights into one affine M, with upper 3x3 A, then
// apply the item's world matrix W (its skeleton entity's), its normal matrix and its sign, as rigid draws do.
//
// The renderer's C++ mirrors these functions (src/Core/Skinning.hpp), and the tests hold the two to each other.
#ifndef FENRIR_SKINNING_GLSL
#define FENRIR_SKINNING_GLSL

#include "Fenrir/Draw.glsl"

// One palette entry: rows 0 to 2 of an affine skinning matrix (model x inverse bind); its last row is (0, 0, 0, 1).
struct FenrirBone
{
    vec4 r0;
    vec4 r1;
    vec4 r2;
};

layout(set = 3, binding = 1, std430) readonly buffer FenrirPalette
{
    FenrirBone bones[];
} fenrirPalette;

// The four bone indices into the item's palette range, and their weights, which sum to 1 or are all 0.
layout(location = 6) in uvec4 inJoints;
layout(location = 7) in vec4 inWeights;

// The blended rows of M. Zero total weight is the identity: the vertex rides rigidly at its bind position.
FenrirBone FenrirSkinRows(DrawItem item)
{
    FenrirBone blended = FenrirBone(vec4(0.0), vec4(0.0), vec4(0.0));
    for (int i = 0; i < 4; ++i)
    {
        FenrirBone bone = fenrirPalette.bones[item.paletteOffset + inJoints[i]];
        blended.r0 += bone.r0 * inWeights[i];
        blended.r1 += bone.r1 * inWeights[i];
        blended.r2 += bone.r2 * inWeights[i];
    }
    if (inWeights.x + inWeights.y + inWeights.z + inWeights.w <= 0.0)
    {
        blended = FenrirBone(vec4(1.0, 0.0, 0.0, 0.0), vec4(0.0, 1.0, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0));
    }
    return blended;
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

// The vertex's world position: W x M x (p, 1).
vec3 FenrirSkinPosition(DrawItem item, vec3 position)
{
    FenrirBone m = FenrirSkinRows(item);
    vec4 p = vec4(position, 1.0);
    vec3 model = vec3(dot(m.r0, p), dot(m.r1, p), dot(m.r2, p));
    return (item.world * vec4(model, 1.0)).xyz;
}

// The vertex's unit world normal: the skinned normal through the item's world normal matrix, as a rigid draw's.
vec3 FenrirSkinNormal(DrawItem item, vec3 normal)
{
    vec3 model = FenrirSkinModelNormal(FenrirSkinLinear(FenrirSkinRows(item)), normal);
    return FenrirNormaliseOr(mat3(item.normal) * model, model);
}

// The vertex's world tangent, unit and perpendicular to worldNormal (FenrirSkinNormal's), with its handedness in w:
// A x t then the world 3x3, Gram-Schmidt'd against the normal; when that is degenerate, the world axis least aligned
// with the normal made perpendicular to it. Handedness is t.w x sign(det A) x sign(det W).
vec4 FenrirSkinTangent(DrawItem item, vec4 tangent, vec3 worldNormal)
{
    mat3 a = FenrirSkinLinear(FenrirSkinRows(item));
    vec3 t = mat3(item.world) * (a * tangent.xyz);
    t -= worldNormal * dot(worldNormal, t);
    vec3 n = abs(worldNormal);
    vec3 axis = n.x <= n.y && n.x <= n.z ? vec3(1.0, 0.0, 0.0) : (n.y <= n.z ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0));
    vec3 perpendicular = normalize(axis - worldNormal * dot(worldNormal, axis));
    vec3 unit = length(t) < 1e-6 ? perpendicular : FenrirNormaliseOr(t, perpendicular);
    float signA = determinant(a) < 0.0 ? -1.0 : 1.0;
    return vec4(unit, tangent.w * signA * item.worldSign);
}

#endif
