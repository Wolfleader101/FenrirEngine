#ifndef FIXTURE_SHARED_GLSL
#define FIXTURE_SHARED_GLSL

#include "Nested.glsl"

vec4 SharedColour()
{
    return material.nestedColour;
}

#endif
