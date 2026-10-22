#version 460
// A texture with a binding but no set.

layout(binding = 1) uniform sampler2D picture;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = texture(picture, vec2(0.0));
}
