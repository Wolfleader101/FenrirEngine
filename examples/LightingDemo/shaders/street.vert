#version 460
#extension GL_GOOGLE_include_directive : require
// LightingDemo's wet streets, vertex stage: a user material shader (the neon city) for the street's flat slab. It
// places the vertex as builtin:lit's mesh.vert does, without skinning; the fragment stage textures the street by its
// world position, so the slab needs no UVs or tangents of its own. The depth prepass runs this stage too and reads the
// world normal at location 1.
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"
#include "Fenrir/View.glsl"

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;

void main()
{
    DrawItem item = CurrentDraw();
    vec4 world = item.world * vec4(inPosition, 1.0);
    vWorldPosition = world.xyz;
    vNormal = mat3(item.normal) * inNormal;
    gl_Position = view.viewProjection * world;
}
