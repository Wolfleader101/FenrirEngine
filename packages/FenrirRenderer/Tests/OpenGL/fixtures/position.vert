#version 460
// Each vertex where its buffer puts it, in clip space with +y up.

layout(location = 0) in vec2 position;

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
}
