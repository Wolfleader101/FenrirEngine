#version 460
// One counter-clockwise triangle covering the whole target at depth 0, the far plane under reversed-Z (ADR-0026), from
// the vertex index alone: no vertex buffer. vNdc is the position in normalised device coordinates, +y up.

layout(location = 0) out vec2 vNdc;

void main()
{
    const vec2 corners[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vNdc = corners[gl_VertexIndex];
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
