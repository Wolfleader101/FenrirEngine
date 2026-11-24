#version 460
#extension GL_GOOGLE_include_directive : require
// The outside project's one material shader, vertex stage. It includes the engine's shared headers, so it compiles only if
// fenrir_add_shaders passes the engine's shader include directory.
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"
#include "Fenrir/View.glsl"

void main()
{
    DrawItem item = CurrentDraw();
    gl_Position = view.viewProjection * item.world * vec4(inPosition, 1.0);
}
