// Fenrir/Draw.glsl: binding set 3 of every material shader, the draw (ADR-0024), for vertex stages. Binding 0 holds
// one record per drawn item; an instanced draw of matching items (ADR-0051) reads them from its base instance on, one
// per instance. Binding 1 holds the copies of every InstancedMeshRenderer's instance list (ADR-0051): such a draw has
// one record, at its base instance, which CurrentDraw composes with each instance's copy. Binding 2 is the skinning
// palette, which Fenrir/Skinning.glsl reads (ADR-0035). Binding 3 is the view's indirection (ADR-0057): for a draw a
// cull pass wrote, one entry per instance naming its record and, for an instance list's copy, the copy, which
// CurrentDraw reads at FenrirDrawIndex() and composes as before; bound to the buffer that holds no indirection, the
// draw is direct and each instance's record is found as above. Every buffer is declared here, whichever a program
// reads, so every program reading one reads them all in the same order: GL numbers a program's storage blocks in the
// order of their bindings, and the palette stays the third whichever program declares it.
//
// Under POINT_SHADOW (ADR-0050), the variant a point light's shadow draws its casters with where the device lets a
// vertex stage write gl_Layer, each item is six instances, one a cube face: CurrentDraw turns its world matrix about the
// light (the view block's camera position) into the face's view and sends its primitives to that face's layer of the
// cube the view block's shadowInfo.z names. Only where the vertex lands is right there; a POINT_SHADOW variant is drawn
// for nothing else. A shader that lists POINT_SHADOW among its keys gets this from CurrentDraw; one that does not is
// drawn into each face by a draw of its own.
#ifndef FENRIR_DRAW_GLSL
#define FENRIR_DRAW_GLSL

#include "Fenrir/DrawRecord.glsl"
#include "Fenrir/SkinMaths.glsl"

#ifdef POINT_SHADOW
#extension GL_ARB_shader_viewport_layer_array : require
#include "Fenrir/CubeFaces.glsl"
#include "Fenrir/View.glsl"
#endif

layout(set = 3, binding = 0, std430) readonly buffer DrawData
{
    DrawItem items[];
} draws;

layout(set = 3, binding = 1, std430) readonly buffer InstanceData
{
    FenrirInstance copies[];
} fenrirInstances;

// The frame's skinning palette (ADR-0035), which Fenrir/Skinning.glsl indexes from a skinned item's paletteOffset.
layout(set = 3, binding = 2, std430) readonly buffer FenrirPalette
{
    FenrirBone bones[];
} fenrirPalette;

// The view's indirection (ADR-0057), or the one entry that says there is none.
layout(set = 3, binding = 3, std430) readonly buffer IndirectionData
{
    uvec2 entries[];
} fenrirVisible;

// Written per backend: Vulkan's instance index counts from the base instance, GL's (its InstanceId) from 0. The
// compiler turns this on in each stage's GL copy and leaves it off in the Vulkan one. Shaders outside this file may not
// read gl_InstanceIndex, which means one or the other.
layout(constant_id = 61440) const bool fenrirInstanceIndexExcludesBase = false;

#ifdef POINT_SHADOW
// The instance's place in its draw, from 0: six for each item, one a face.
uint FenrirCubeInstance()
{
    return uint(gl_InstanceIndex) - (fenrirInstanceIndexExcludesBase ? 0u : uint(gl_BaseInstance));
}
#endif

// The instance being drawn: the draw's base instance plus its instance index (over six, one a face, under
// POINT_SHADOW). For a direct draw it is the instance's record; for an indirect draw a cull pass wrote (ADR-0057) it is
// the instance's entry in the view's indirection, which names the record. It holds for an indirect draw unchanged:
// each command's baseInstance reaches gl_BaseInstance as a direct draw's firstInstance does, on GL from the command the
// GPU read.
uint FenrirDrawIndex()
{
#ifdef POINT_SHADOW
    return uint(gl_BaseInstance) + FenrirCubeInstance() / 6u;
#else
    return uint(gl_InstanceIndex) + (fenrirInstanceIndexExcludesBase ? uint(gl_BaseInstance) : 0u);
#endif
}

#ifdef POINT_SHADOW
// `item` turned into its instance's cube face, about the light at the view block's camera position, whose view block
// looks down -Z from there: the face's turn, then the light back where it was. Its primitives go to that face of the
// view's cube.
DrawItem FenrirCubeFaceOf(DrawItem item)
{
    uint face = FenrirCubeInstance() % 6u;
    mat3 turn = FenrirFaceTurn(face);
    vec3 light = view.cameraPosition.xyz;
    mat4 toFace = mat4(turn);
    toFace[3] = vec4(light - turn * light, 1.0);
    item.world = toFace * item.world;
    item.normal = mat4(turn) * item.normal;
    item.worldSign = -item.worldSign;
    gl_Layer = int(view.shadowInfo.z * 6u + face);
    return item;
}
#endif

// The record of the item this instance is drawing. An instance list's draw has one record for all its copies: this
// instance's copy is composed into it, its transform after the entity's world matrix. Which record and copy: for a
// direct draw, the record at FenrirDrawIndex(), or the one at the base instance composed with the copy FenrirDrawIndex()
// counts to from it; for an indirect draw, what the indirection's entry at FenrirDrawIndex() names.
DrawItem CurrentDraw()
{
    uint index = FenrirDrawIndex();
    uint record;
    uint copy;
    if (fenrirVisible.entries[0].x == FenrirNoIndirection)
    {
        DrawItem first = draws.items[gl_BaseInstance];
        if (first.instanceCount == 0u)
        {
            record = index;
            copy = FenrirNoCopy;
        }
        else
        {
            record = uint(gl_BaseInstance);
            copy = first.instanceFirst + index - uint(gl_BaseInstance);
        }
    }
    else
    {
        uvec2 entry = fenrirVisible.entries[index];
        record = entry.x;
        copy = entry.y;
    }
    DrawItem item = draws.items[record];
    if (copy != FenrirNoCopy)
    {
        FenrirInstance instance = fenrirInstances.copies[copy];
        item.world = item.world * instance.transform;
        item.normal = item.normal * mat4(instance.normal);
        item.worldSign *= determinant(mat3(instance.transform)) < 0.0 ? -1.0 : 1.0;
        item.colour *= instance.colour;
    }
#ifdef POINT_SHADOW
    return FenrirCubeFaceOf(item);
#else
    return item;
#endif
}

#endif
