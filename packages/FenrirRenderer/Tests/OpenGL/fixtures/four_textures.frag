#version 460
// A material with a MaterialParams block and four textures in set 2, as builtin:lit has: each output channel is one
// texture's red at the centre times the block's scale, so a texture bound at the wrong GL unit shows in its channel.

#pragma fenrir default(scale, 1.0, 1.0, 1.0, 1.0)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 scale;
} material;
layout(set = 2, binding = 1) uniform sampler2D first;
layout(set = 2, binding = 2) uniform sampler2D second;
layout(set = 2, binding = 3) uniform sampler2D third;
layout(set = 2, binding = 4) uniform sampler2D fourth;

layout(location = 0) out vec4 outColour;

void main()
{
    const vec2 centre = vec2(0.5);
    outColour = material.scale * vec4(texture(first, centre).r, texture(second, centre).r, texture(third, centre).r,
                                      texture(fourth, centre).r);
}
