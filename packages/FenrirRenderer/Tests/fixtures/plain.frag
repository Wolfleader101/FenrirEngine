#version 460
// A fragment stage with no bindings: white.

layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vec4(1.0);
}
