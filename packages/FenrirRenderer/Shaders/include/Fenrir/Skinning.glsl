// Fenrir/Skinning.glsl: linear blend skinning for vertex stages (ADR-0035), under the SKINNED variant key. It reads
// the palette Fenrir/Draw.glsl declares (set 3, binding 2), declares the joints and weights at vertex locations 6 and
// 7, and gives FenrirSkinPosition, FenrirSkinNormal and FenrirSkinTangent, each taking the current draw's record and
// returning world space. They blend the four palette entries the vertex names by its weights into one affine M, with upper 3x3 A
// (Fenrir/SkinMaths.glsl), then apply the item's world matrix W (its skeleton entity's), its normal matrix and its sign,
// as rigid draws do.
//
// With compute skinning on (ADR-0050), the renderer draws the built-in programs' skinned meshes rigidly from what
// builtin:skinning made of them; a user shader's SKINNED variant is drawn either way.
//
// The renderer's C++ mirrors these functions (src/Core/Skinning.hpp), and the tests hold the two to each other.
#ifndef FENRIR_SKINNING_GLSL
#define FENRIR_SKINNING_GLSL

#include "Fenrir/Draw.glsl"
#include "Fenrir/SkinMaths.glsl"

// The four bone indices into the item's palette range, and their weights, which sum to 1 or are all 0.
layout(location = 6) in uvec4 inJoints;
layout(location = 7) in vec4 inWeights;

// The blended rows of M.
FenrirBone FenrirSkinRows(DrawItem item)
{
    return FenrirBlendBones(fenrirPalette.bones[item.paletteOffset + inJoints[0]],
                            fenrirPalette.bones[item.paletteOffset + inJoints[1]],
                            fenrirPalette.bones[item.paletteOffset + inJoints[2]],
                            fenrirPalette.bones[item.paletteOffset + inJoints[3]], inWeights);
}

// The vertex's world position: W x M x (p, 1).
vec3 FenrirSkinPosition(DrawItem item, vec3 position)
{
    return (item.world * vec4(FenrirSkinPoint(FenrirSkinRows(item), position), 1.0)).xyz;
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
    vec3 t = FenrirPerpendicularTangent(mat3(item.world) * (a * tangent.xyz), worldNormal);
    return vec4(t, tangent.w * FenrirSkinSign(a) * item.worldSign);
}

#endif
