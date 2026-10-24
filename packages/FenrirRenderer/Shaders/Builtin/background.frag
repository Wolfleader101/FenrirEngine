#version 460
#extension GL_GOOGLE_include_directive : require
// The Background pass (ADR-0019, ADR-0026): the view's background drawn at depth 0 after the opaque items, depth
// tested, so only pixels nothing covered take it. The base variant fills them with one colour: the scene's Sky colour,
// or the camera's own. The CUBEMAP variant samples the scene's Sky cubemap by the direction each pixel looks along.

#ifdef CUBEMAP
#include "Fenrir/View.glsl"

layout(set = 2, binding = 0) uniform samplerCube sky;
#else
#pragma fenrir default(colour, 0.0, 0.0, 0.0, 1.0)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA.
    vec4 colour;
} background;
#endif

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

void main()
{
#ifdef CUBEMAP
    // Two points along the pixel's ray, at the near plane (depth 1) and further on (depth 0.5): the direction between
    // them is the one the pixel looks along, and in an orthographic view it is the view's own.
    vec4 near = view.inverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec4 far = view.inverseViewProjection * vec4(vNdc, 0.5, 1.0);
    vec3 direction = far.xyz / far.w - near.xyz / near.w;
    // Sampled by world direction, with the faces laid out as GL and D3D lay out a cubemap's.
    outColour = vec4(texture(sky, direction).rgb, 1.0);
#else
    outColour = background.colour;
#endif
    // Depth 0, as the full-screen triangle already is, written so the depth test runs after the shader. With several
    // views, each view's passes run between other views' (by render stage), and NVIDIA's early depth culling then
    // sometimes passed this pass over a view's opaque items when it came back to that view's SceneDepth; the late test
    // against the stored depth is right.
    gl_FragDepth = 0.0;
}
