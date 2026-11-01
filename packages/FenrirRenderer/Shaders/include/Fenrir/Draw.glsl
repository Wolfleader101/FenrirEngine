// Fenrir/Draw.glsl: binding set 3 of every material shader, the draw (ADR-0024), for vertex stages. Binding 0 holds
// one record per drawn item; an instanced draw of matching items (ADR-0051) reads them from its base instance on, one
// per instance. Binding 1 holds the copies of every InstancedMeshRenderer's instance list (ADR-0051): such a draw has
// one record, at its base instance, which CurrentDraw composes with each instance's copy. Binding 2 is the skinning
// palette, which Fenrir/Skinning.glsl declares (ADR-0035). The instance-list buffer is declared beside the records, which
// CurrentDraw reads with it, so every program reading one reads both: GL numbers a program's storage blocks in the
// order of their bindings, and the palette stays the third whichever program declares it.
#ifndef FENRIR_DRAW_GLSL
#define FENRIR_DRAW_GLSL

struct DrawItem
{
    // Local to world; for a skinned item, its skeleton entity's world matrix.
    mat4 world;
    // The inverse transpose of world, for normals and tangents.
    mat4 normal;
    // A skinned item's first palette entry, in bones (Fenrir/Skinning.glsl).
    uint paletteOffset;
    // The sign of world's determinant: -1 when it mirrors, else 1, for tangent handedness.
    float worldSign;
    // An instance list's first copy at binding 1, and how many copies it draws; 0 copies for every other item.
    uint instanceFirst;
    uint instanceCount;
    // Linear, which the built-in programs multiply the vertex colour by: white, or an instance list copy's colour.
    vec4 colour;
};

// One copy of an instance list, relative to the drawing entity.
struct FenrirInstance
{
    mat4 transform;
    // The inverse transpose of transform's 3x3.
    mat3 normal;
    vec4 colour;
};

layout(set = 3, binding = 0, std430) readonly buffer DrawData
{
    DrawItem items[];
} draws;

// Written per backend: Vulkan's instance index counts from the base instance, GL's (its InstanceId) from 0. The
// compiler turns this on in each stage's GL copy and leaves it off in the Vulkan one. Shaders outside this file may not
// read gl_InstanceIndex, which means one or the other.
layout(constant_id = 61440) const bool fenrirInstanceIndexExcludesBase = false;

// The record of the instance being drawn: the draw's base instance plus its instance index.
uint FenrirDrawIndex()
{
    return uint(gl_InstanceIndex) + (fenrirInstanceIndexExcludesBase ? uint(gl_BaseInstance) : 0u);
}

layout(set = 3, binding = 1, std430) readonly buffer InstanceData
{
    FenrirInstance copies[];
} fenrirInstances;

// The record of the item this instance is drawing. An instance list's draw has one record for all its copies: this
// instance's copy is composed into it, its transform after the entity's world matrix.
DrawItem CurrentDraw()
{
    DrawItem first = draws.items[gl_BaseInstance];
    if (first.instanceCount == 0u)
    {
        return draws.items[FenrirDrawIndex()];
    }
    FenrirInstance copy = fenrirInstances.copies[first.instanceFirst + FenrirDrawIndex() - uint(gl_BaseInstance)];
    first.world = first.world * copy.transform;
    first.normal = first.normal * mat4(copy.normal);
    first.worldSign *= determinant(mat3(copy.transform)) < 0.0 ? -1.0 : 1.0;
    first.colour *= copy.colour;
    return first;
}

#endif
