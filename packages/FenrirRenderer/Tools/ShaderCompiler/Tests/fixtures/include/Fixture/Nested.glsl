#ifndef FIXTURE_NESTED_GLSL
#define FIXTURE_NESTED_GLSL

// A pragma in an included file applies to the programs that include it.
#pragma fenrir default(nestedColour, 0.25, 0.5, 0.75, 1.0)
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    vec4 nestedColour;
} material;

#endif
