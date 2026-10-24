#version 460
// The fragment stage of builtin:debug-lines and builtin:debug-triangles (ADR-0025): the vertex's linear colour, which
// the pipeline alpha-blends over SceneColor.

layout(location = 0) in vec4 vColour;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vColour;
}
