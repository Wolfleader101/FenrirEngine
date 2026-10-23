#version 460
// The Output pass (ADR-0019), the placeholder tonemap post-processing replaces: the view's linear SceneColor clamped
// to [0, 1], drawn into the view's viewport of its render target. The window's backbuffer and image targets are sRGB,
// so the write encodes it. With the MSAA key, SceneColor is multisampled and this resolves it: each pixel is the mean
// of its samples.

#ifdef MSAA
layout(set = 2, binding = 0) uniform sampler2DMS sceneColour;
#else
layout(set = 2, binding = 0) uniform sampler2D sceneColour;
#endif

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColour;

void main()
{
    // NDC +y is up and a texture's row 0 is its top row, on every backend (ADR-0016).
    vec2 uv = vec2(vNdc.x * 0.5 + 0.5, 0.5 - vNdc.y * 0.5);
#ifdef MSAA
    ivec2 size = textureSize(sceneColour);
    ivec2 texel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    int samples = textureSamples(sceneColour);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < samples; ++i)
    {
        sum += texelFetch(sceneColour, texel, i).rgb;
    }
    vec3 colour = sum / float(samples);
#else
    vec3 colour = texture(sceneColour, uv).rgb;
#endif
    outColour = vec4(clamp(colour, 0.0, 1.0), 1.0);
}
