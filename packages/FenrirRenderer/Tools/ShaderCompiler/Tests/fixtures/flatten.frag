#version 460
// With flatten.vert: a uniform block at set 2 binding 0 (material), textures at set 2 bindings 1 and 3 (first, second),
// and a storage block at set 3 binding 2 (lights).

layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 tint;
} material;
layout(set = 2, binding = 1) uniform sampler2D first;
layout(set = 2, binding = 3) uniform sampler2D second;
layout(set = 3, binding = 2, std430) readonly buffer Lights
{
    vec4 colours[];
} lights;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColour;

void main()
{
    outColour = material.tint * texture(first, vUv) * texture(second, vUv) + lights.colours[0];
}
