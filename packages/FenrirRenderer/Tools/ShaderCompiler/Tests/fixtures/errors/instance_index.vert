#version 460
// gl_InstanceIndex has no OpenGL equivalent.

void main()
{
    gl_Position = vec4(float(gl_InstanceIndex), 0.0, 0.0, 1.0);
}
