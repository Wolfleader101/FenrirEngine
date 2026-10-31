#version 460
// A vertex stage whose set 0 binding 0 is not the renderer's frame block: 16 bytes where FrameData is 64.

layout(set = 0, binding = 0, std140) uniform FrameData
{
    vec4 tint;
} frame;

layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position = vec4(inPosition, 1.0) + frame.tint;
}
