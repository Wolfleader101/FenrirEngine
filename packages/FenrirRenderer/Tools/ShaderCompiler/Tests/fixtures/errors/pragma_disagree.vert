#version 460
// The vertex stage's default for strength, which pragma_disagree.frag contradicts.

layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    float strength;
} material;
#pragma fenrir default(strength, 1.0)

void main()
{
    gl_Position = vec4(material.strength);
}
