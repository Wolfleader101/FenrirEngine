#version 460
// A material shader sampling one texture parameter, `picture`, at its centre: white by default. With the built-in mesh
// vertex stage, it stands in for any material that samples an image, such as a camera's image target (ADR-0026).

#pragma fenrir default(picture, white)
layout(set = 2, binding = 0) uniform sampler2D picture;

layout(location = 0) out vec4 outColour;

void main()
{
    outColour = texture(picture, vec2(0.5, 0.5));
}
