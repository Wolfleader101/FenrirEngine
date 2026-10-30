#version 460
#extension GL_GOOGLE_include_directive : require
// AnimationDemo's flat shader, vertex stage: a user material shader built with no SKINNED key, so a skinned mesh given
// it draws with the fallback material instead (scene 3, ADR-0035). It draws rigid meshes as usual.
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"
#include "Fenrir/View.glsl"

void main()
{
    DrawItem item = CurrentDraw();
    gl_Position = view.viewProjection * item.world * vec4(inPosition, 1.0);
}
