// Fenrir/Vertex.glsl: the standard vertex locations, for vertex stages. A mesh stream the shader reads but the mesh
// lacks binds a constant default instead (normal +Y, UV 0, colour white). Locations 6 and 7 are a skinned mesh's
// joints and weights, which Fenrir/Skinning.glsl declares (ADR-0035).
#ifndef FENRIR_VERTEX_GLSL
#define FENRIR_VERTEX_GLSL

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
// xyz: the tangent; w: the bitangent's sign.
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUv0;
layout(location = 4) in vec2 inUv1;
// Linear RGBA.
layout(location = 5) in vec4 inColour;

#endif
