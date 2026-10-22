#version 460
// The Output pass (ADR-0019), the placeholder tonemap post-processing replaces: the view's linear SceneColor clamped
// to [0, 1]. The backbuffer is sRGB, so the write encodes it.

layout(set = 2, binding = 0) uniform sampler2D sceneColour;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

void main()
{
    // NDC +y is up and a texture's row 0 is its top row, on every backend (ADR-0016).
    vec2 uv = vec2(vNdc.x * 0.5 + 0.5, 0.5 - vNdc.y * 0.5);
    outColour = vec4(clamp(texture(sceneColour, uv).rgb, 0.0, 1.0), 1.0);
}
