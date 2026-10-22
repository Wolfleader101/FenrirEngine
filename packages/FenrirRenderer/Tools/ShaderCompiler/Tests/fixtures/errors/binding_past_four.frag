#version 460
// A texture at binding 4: a set holds bindings 0 to 3 of a kind.

layout(set = 2, binding = 4) uniform sampler2D picture;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = texture(picture, vec2(0.0));
}
