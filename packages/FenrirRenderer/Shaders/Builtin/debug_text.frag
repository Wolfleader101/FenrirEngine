#version 460
// builtin:debug-text, the fragment stage (ADR-0025): a glyph from the debug font's signed-distance-field atlas, 0.5 on
// its edge. The edge is smoothed over about a screen pixel, measured by the field's own screen-space rate of change, so
// text stays crisp at any size and content scale. A thin dark outline keeps it readable over light and dark alike.

layout(set = 1, binding = 0) uniform sampler2D fontAtlas;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColour;
layout(location = 0) out vec4 outColour;

void main()
{
    float distance = texture(fontAtlas, vUv).r;
    float smoothing = max(fwidth(distance) * 0.7, 1e-4);
    float fill = smoothstep(0.5 - smoothing, 0.5 + smoothing, distance);
    float outline = smoothstep(0.38 - smoothing, 0.38 + smoothing, distance);
    if (outline <= 0.0)
    {
        discard;
    }
    // Straight alpha: the glyph's colour inside, black in the outline round it.
    outColour = vec4(vColour.rgb * (fill / outline), vColour.a * outline);
}
