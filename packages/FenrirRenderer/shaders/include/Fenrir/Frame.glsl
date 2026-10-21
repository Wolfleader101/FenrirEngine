// SPIKE: set 0, the frame (ADR-0024): the one directional light and ambient.
layout(set = 0, binding = 0, std140) uniform FrameData
{
    vec4 lightDirection; // xyz: the way the light travels (world), w unused
    vec4 lightColour;    // rgb: colour * intensity
    vec4 ambient;        // rgb: colour * intensity
    vec4 time;           // x: seconds
} frame;
