#version 460
#extension GL_GOOGLE_include_directive : require
// Each vertex under its draw record's world matrix, the record read through Fenrir/Draw.glsl at FenrirDrawIndex(): an
// indirect command's base instance picks the record as a direct draw's does (ADR-0057), in clip space with +y up.
#include "Fenrir/Draw.glsl"

layout(location = 0) in vec2 position;

void main()
{
    gl_Position = CurrentDraw().world * vec4(position, 0.0, 1.0);
}
