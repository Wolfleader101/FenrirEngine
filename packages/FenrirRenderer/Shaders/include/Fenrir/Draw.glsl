// Fenrir/Draw.glsl: binding set 3 of every material shader, the draw (ADR-0024), for vertex stages. Binding 0 holds
// one record per drawn item, indexed by the draw's base instance (gl_InstanceIndex is refused: GL has no equivalent).
// Binding 1 is left free for the skinning palette buffer the animation spec adds (ADR-0035).
#ifndef FENRIR_DRAW_GLSL
#define FENRIR_DRAW_GLSL

struct DrawItem
{
    // Local to world.
    mat4 world;
    // The inverse transpose of world, for normals and tangents.
    mat4 normal;
};

layout(set = 3, binding = 0, std430) readonly buffer DrawData
{
    DrawItem items[];
} draws;

// The record of the item this draw is drawing.
DrawItem CurrentDraw()
{
    return draws.items[gl_BaseInstance];
}

#endif
