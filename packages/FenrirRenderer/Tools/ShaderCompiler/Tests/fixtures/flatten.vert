#version 460
#extension GL_GOOGLE_include_directive : require
// Every resource kind across the sets, with flatten.frag: uniform blocks at set 0 binding 0 (frame) and set 1 binding 2
// (extra), a storage block at set 3 binding 0 (draws). Reads gl_VertexIndex, which the GL copy retargets.
#include "Fenrir/Frame.glsl"
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"

layout(set = 1, binding = 2, std140) uniform Extra
{
    vec4 offset;
} extra;

layout(location = 0) out vec2 vUv;

void main()
{
    DrawItem item = CurrentDraw();
    vUv = inUv0 + vec2(float(gl_VertexIndex));
    gl_Position = item.world * vec4(inPosition, 1.0) + extra.offset + frame.time;
}
