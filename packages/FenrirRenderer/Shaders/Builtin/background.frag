#version 460
// The Background pass (ADR-0019): a clear colour drawn at depth 0 after the opaque items, depth tested, so only pixels
// nothing covered take it. The sky (renderer ticket 15) replaces the colour with the scene's Sky.

#pragma fenrir default(colour, 0.0, 0.0, 0.0, 1.0)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA.
    vec4 colour;
} background;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = background.colour;
}
