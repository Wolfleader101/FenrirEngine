// Fenrir/DrawRecord.glsl: the records of binding set 3, the draw (ADR-0024, ADR-0051, ADR-0057), as data alone, for any
// stage: a drawn item's record, one copy of an instance list, and one entry of a view's indirection. Fenrir/Draw.glsl
// declares the buffers holding them for vertex stages and reads them with CurrentDraw(); builtin:cull writes the
// indirection's entries in compute. The renderer's C++ mirrors each layout (src/Core/SceneRenderer.cpp) and pins it
// with static_asserts.
#ifndef FENRIR_DRAW_RECORD_GLSL
#define FENRIR_DRAW_RECORD_GLSL

// One drawn item, as the draw buffer (set 3, binding 0) holds it: 192 bytes in std430.
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

// One copy of an instance list (set 3, binding 1), relative to the drawing entity: 128 bytes in std430.
struct FenrirInstance
{
    mat4 transform;
    // The inverse transpose of transform's 3x3.
    mat3 normal;
    vec4 colour;
};

// A view's indirection (set 3, binding 3, ADR-0057) is one uvec2 per instance a cull pass kept: x the record it draws,
// y the copy at binding 1 it composes into the record, or FenrirNoCopy for a lone or merged item. An indirect draw's
// instances index it from the command's base instance. A buffer whose first entry's record is FenrirNoIndirection
// holds no indirection: the draws bound to it are direct, each instance its own record as before.
const uint FenrirNoCopy = 0xFFFFFFFFu;
const uint FenrirNoIndirection = 0xFFFFFFFFu;

#endif
