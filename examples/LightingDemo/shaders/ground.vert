#version 460
#extension GL_GOOGLE_include_directive : require
// LightingDemo's forest floor, vertex stage: a user material shader (the forest clearing) for the generated ground mesh.
// It places the vertex as builtin:lit's mesh.vert does, without skinning, and passes the ground's layer weights (the
// vertex colour: red under the trees, green on the path) and its tangent on to the fragment stage.
#include "Fenrir/Draw.glsl"
#include "Fenrir/Vertex.glsl"
#include "Fenrir/View.glsl"

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vColour;
layout(location = 3) out vec2 vUv;
layout(location = 4) out vec4 vTangent;

void main()
{
    DrawItem item = CurrentDraw();
    vec4 world = item.world * vec4(inPosition, 1.0);
    vWorldPosition = world.xyz;
    vNormal = mat3(item.normal) * inNormal;
    vTangent = vec4(mat3(item.world) * inTangent.xyz, inTangent.w * item.worldSign);
    vColour = inColour;
    vUv = inUv0;
    gl_Position = view.viewProjection * world;
}
