#version 460
#extension GL_GOOGLE_include_directive : require
// Includes from the engine's directory and from a directory fenrir_add_shaders names; Fixture/Shared.glsl includes
// Nested.glsl beside itself.
#include "Fenrir/View.glsl"
#include <Fixture/Shared.glsl>

layout(location = 0) out vec4 outColour;

void main()
{
    outColour = SharedColour() * view.viewport;
}
