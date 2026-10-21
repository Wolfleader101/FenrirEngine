#version 460
// The placeholder tonemap (ADR-0019): clamp; the sRGB backbuffer encodes.
layout(set = 2, binding = 0) uniform sampler2D sceneColour;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

void main()
{
    // NDC +y is up and a texture's first row is its top, on every backend (ADR-0016).
    vec2 uv = vec2(vNdc.x * 0.5 + 0.5, 0.5 - vNdc.y * 0.5);
    outColour = vec4(clamp(texture(sceneColour, uv).rgb, 0.0, 1.0), 1.0);
}
