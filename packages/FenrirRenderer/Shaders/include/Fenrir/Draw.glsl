// Fenrir/Draw.glsl: binding set 3 of every material shader, the draw (ADR-0024), for vertex stages. Binding 0 holds
// one record per drawn item; an instanced draw of matching items (ADR-0051) reads them from its base instance on, one
// per instance. Binding 1 holds the copies of every InstancedMeshRenderer's instance list (ADR-0051): such a draw has
// one record, at its base instance, which CurrentDraw composes with each instance's copy. Binding 2 is the skinning
// palette, which Fenrir/Skinning.glsl declares (ADR-0035). The instance-list buffer is declared beside the records, which
// CurrentDraw reads with it, so every program reading one reads both: GL numbers a program's storage blocks in the
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

#ifdef POINT_SHADOW
#extension GL_ARB_shader_viewport_layer_array : require
#include "Fenrir/CubeFaces.glsl"
#include "Fenrir/View.glsl"
#endif

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
    // The item's box for culling (ADR-0057), xyz, w unused: for a lone or merged item its mesh's bounds with world
    // already applied (a skinned item's skinned bounds), so a cull tests it under the identity; for an instance list,
    // the mesh's local bounds, which each copy's transform and world move.
    vec4 boundsMin;
    vec4 boundsMax;
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

#ifdef POINT_SHADOW
// The instance's place in its draw, from 0: six for each item, one a face.
uint FenrirCubeInstance()
{
    return uint(gl_InstanceIndex) - (fenrirInstanceIndexExcludesBase ? 0u : uint(gl_BaseInstance));
}
#endif

// The record of the instance being drawn: the draw's base instance plus its instance index (over six, one a face,
// under POINT_SHADOW). It holds for an indirect draw unchanged (ADR-0057): each command's baseInstance reaches
// gl_BaseInstance as a direct draw's firstInstance does, on GL from the command the GPU read.
uint FenrirDrawIndex()
{
#ifdef POINT_SHADOW
    return uint(gl_BaseInstance) + FenrirCubeInstance() / 6u;
#else
    return uint(gl_InstanceIndex) + (fenrirInstanceIndexExcludesBase ? uint(gl_BaseInstance) : 0u);
#endif
}

layout(set = 3, binding = 1, std430) readonly buffer InstanceData
{
    FenrirInstance copies[];
} fenrirInstances;

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
// instance's copy is composed into it, its transform after the entity's world matrix.
DrawItem CurrentDraw()
{
    DrawItem first = draws.items[gl_BaseInstance];
    if (first.instanceCount == 0u)
    {
#ifdef POINT_SHADOW
        return FenrirCubeFaceOf(draws.items[FenrirDrawIndex()]);
#else
        return draws.items[FenrirDrawIndex()];
#endif
    }
    FenrirInstance copy = fenrirInstances.copies[first.instanceFirst + FenrirDrawIndex() - uint(gl_BaseInstance)];
    first.world = first.world * copy.transform;
    first.normal = first.normal * mat4(copy.normal);
    first.worldSign *= determinant(mat3(copy.transform)) < 0.0 ? -1.0 : 1.0;
    first.colour *= copy.colour;
#ifdef POINT_SHADOW
    return FenrirCubeFaceOf(first);
#else
    return first;
#endif
}

#endif
