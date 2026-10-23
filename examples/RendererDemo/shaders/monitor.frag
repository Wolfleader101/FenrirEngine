#version 460
// RendererDemo's monitor screen: the image `screen` shown unlit, as a screen glows. In the "Cameras and render targets"
// scene it is the image a third camera draws into (ADR-0026). UV (0, 0) is the image's top-left corner.

#pragma fenrir default(screen, black)
layout(set = 2, binding = 0) uniform sampler2D screen;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vec4(texture(screen, vUv).rgb, 1.0);
}
