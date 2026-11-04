// Fenrir/Vertex.glsl: the standard vertex locations, for vertex stages. A mesh stream the shader reads but the mesh
// lacks binds a constant default instead (normal +Y, UV 0, colour white, and the zero tangent, whose w of 0 says the mesh
// has no tangents: ADR-0048). Locations 6 and 7 are a skinned mesh's joints and weights, which Fenrir/Skinning.glsl
// declares (ADR-0035).
//
// gl_Position is invariant: the depth prepass runs a vertex stage with another fragment stage than Opaque does, and
// Opaque tests the depth it laid down for equality (ADR-0049), so both programs must place every vertex alike.
#ifndef FENRIR_VERTEX_GLSL
#define FENRIR_VERTEX_GLSL

invariant gl_Position;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
// xyz: the tangent; w: the bitangent's sign, cross(normal, tangent) x w, or 0 for a mesh without tangents.
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUv0;
layout(location = 4) in vec2 inUv1;
// Linear RGBA.
layout(location = 5) in vec4 inColour;

#endif
