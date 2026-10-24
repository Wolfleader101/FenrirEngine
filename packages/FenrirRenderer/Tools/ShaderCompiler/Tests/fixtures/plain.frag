#version 460
// A fragment stage with no resources, paired with the vertex fixtures.

layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vec4(1.0);
}
