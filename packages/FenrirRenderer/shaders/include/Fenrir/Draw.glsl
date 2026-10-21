// SPIKE: set 3, the draw (ADR-0024): per-item matrices, indexed by base instance.
struct DrawItem
{
    mat4 world;
    mat4 normal;
};

layout(set = 3, binding = 0, std430) readonly buffer DrawData
{
    DrawItem items[];
} draws;

DrawItem CurrentDraw()
{
    return draws.items[gl_BaseInstance];
}
