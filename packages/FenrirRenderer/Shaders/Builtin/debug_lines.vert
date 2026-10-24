#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:debug-lines, the vertex stage (ADR-0025): each debug line, read from set 2 by gl_VertexIndex / 6, expanded
// into a quad of two triangles `thickness` pixels across in the view's viewport, since GL core has no wide lines. Each
// end is capped by half the thickness, so a line of zero length is a square dot. A line crossing the near plane is cut
// there; one wholly behind it draws nothing.
#include "Fenrir/View.glsl"

struct DebugLine
{
    // xyz: the start, world space; w: the thickness in pixels.
    vec4 from;
    // xyz: the end, world space; w unused.
    vec4 to;
    // x, y: the start's and the end's colours, sRGB-encoded RGBA8 (red in the low byte). zw unused.
    uvec4 colours;
};

layout(set = 2, binding = 0, std430) readonly buffer DebugLines
{
    DebugLine lines[];
} debugLines;

layout(location = 0) out vec4 vColour;

vec4 DecodeColour(uint packed)
{
    vec4 c = unpackUnorm4x8(packed);
    vec3 linear = mix(c.rgb / 12.92, pow((c.rgb + 0.055) / 1.055, vec3(2.4)), greaterThan(c.rgb, vec3(0.04045)));
    return vec4(linear, c.a);
}

void main()
{
    DebugLine line = debugLines.lines[gl_VertexIndex / 6];
    int corner = gl_VertexIndex % 6;
    // Two triangles, (start, end, end) and (start, end, start), on the two sides of the line.
    bool atEnd = corner == 1 || corner == 2 || corner == 4;
    float side = (corner == 2 || corner == 4 || corner == 5) ? 1.0 : -1.0;

    vec4 a = view.viewProjection * vec4(line.from.xyz, 1.0);
    vec4 b = view.viewProjection * vec4(line.to.xyz, 1.0);
    // Inside the near plane is depth at most 1 under reversed-Z: z <= w.
    float insideA = a.w - a.z;
    float insideB = b.w - b.z;
    if (insideA < 0.0 && insideB < 0.0)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vColour = vec4(0.0);
        return;
    }
    if (insideA < 0.0)
    {
        a = mix(a, b, insideA / (insideA - insideB));
    }
    else if (insideB < 0.0)
    {
        b = mix(b, a, insideB / (insideB - insideA));
    }

    vec2 halfViewport = view.viewport.zw * 0.5;
    vec2 pixelA = a.xy / a.w * halfViewport;
    vec2 pixelB = b.xy / b.w * halfViewport;
    vec2 along = pixelB - pixelA;
    float span = length(along);
    vec2 direction = span > 1e-4 ? along / span : vec2(1.0, 0.0);
    vec2 across = vec2(-direction.y, direction.x);
    float halfThickness = line.from.w * 0.5;
    vec2 offset = (across * side + direction * (atEnd ? 1.0 : -1.0)) * halfThickness;

    vec4 position = atEnd ? b : a;
    position.xy += offset / halfViewport * position.w;
    // A hair nearer than the surface it lies on (reversed-Z: larger is nearer), so a wire over a solid is not lost in it.
    position.z *= 1.0005;
    gl_Position = position;
    vColour = DecodeColour(atEnd ? line.colours.y : line.colours.x);
}
