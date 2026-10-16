#version 460 core
// World-space lines, each vertex with its own colour (physics debug lines).
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColour;

uniform mat4 view;
uniform mat4 projection;

out vec3 colour;

void main()
{
    colour = aColour;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
