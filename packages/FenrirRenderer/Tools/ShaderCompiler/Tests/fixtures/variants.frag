#version 460
// Built with the keys FIRST and SECOND: each adds a member, and SECOND gives strength a default that the variants
// without it do not have.

layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    float strength;
#ifdef FIRST
    float first;
#endif
#ifdef SECOND
    float second;
#endif
} material;

#ifdef SECOND
#pragma fenrir default(strength, 7.0)
#endif

layout(location = 0) out vec4 outColour;

void main()
{
    outColour = vec4(material.strength);
#ifdef FIRST
    outColour.x += material.first;
#endif
#ifdef SECOND
    outColour.y += material.second;
#endif
}
