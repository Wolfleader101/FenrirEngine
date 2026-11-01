// Fenrir/Draw.glsl: binding set 3 of every material shader, the draw (ADR-0024), for vertex stages. Binding 0 holds
// one record per drawn item; an instanced draw of matching items (ADR-0051) reads them from its base instance on, one
// per instance. Binding 1 is the skinning palette, which Fenrir/Skinning.glsl declares (ADR-0035).
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

// The record of the item this instance is drawing.
DrawItem CurrentDraw()
{
    return draws.items[FenrirDrawIndex()];
}

#endif
