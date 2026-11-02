#version 460
#extension GL_GOOGLE_include_directive : require
// LightingDemo's building facades, vertex stage: a user material shader (the neon city) for the unit cube, drawn once
// per building of an instance list whose copies are each a building's footprint and height as a scale and its place on
// the ground. It places the vertex as builtin:lit's mesh.vert does, without skinning, and passes the fragment stage
// where on its building the vertex is, in metres from the building's centre, the building's half size, and its copy's
// colour (the windows' light, and in alpha the share of them lit). The depth prepass runs this stage too and reads the
// world normal at location 1.
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"
#include "Fenrir/View.glsl"

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vColour;
layout(location = 3) out vec3 vLocal;
layout(location = 4) flat out vec3 vHalfSize;
layout(location = 5) flat out vec3 vCentre;

void main()
{
    DrawItem item = CurrentDraw();
    vec4 world = item.world * vec4(inPosition, 1.0);
    // The cube is a unit one about its origin, so a building's size is its world matrix's column lengths; the
    // buildings are never turned.
    vec3 size = vec3(length(item.world[0].xyz), length(item.world[1].xyz), length(item.world[2].xyz));
    vWorldPosition = world.xyz;
    vNormal = mat3(item.normal) * inNormal;
    vColour = item.colour;
    vLocal = inPosition * size;
    vHalfSize = size * 0.5;
    vCentre = item.world[3].xyz;
    gl_Position = view.viewProjection * world;
}
