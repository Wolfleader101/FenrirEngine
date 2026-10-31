// Fenrir/Frame.glsl: binding set 0 of every material shader, the frame (ADR-0024, ADR-0048). Written once per frame by
// the renderer, which declares the same layout in C++ and checks it against a shader's reflection in debug builds.
#ifndef FENRIR_FRAME_GLSL
#define FENRIR_FRAME_GLSL

layout(set = 0, binding = 0, std140) uniform FrameData
{
    // xyz: the way the directional light travels, in world space (its entity's world -Z), unit length. w unused.
    vec4 lightDirection;
    // rgb: the directional light's colour times its intensity, linear; black with no light. a unused.
    vec4 lightColour;
    // x: seconds since the app started; y: this frame's delta in seconds. zw unused.
    vec4 time;
    // The scene's Sky, as drawn behind everything (ADR-0047). x: its intensity; y and z: the cosine and sine of its
    // rotation about +Y. w unused.
    vec4 sky;
    // The sky the scene is lit by: its SkyLight's, else its Sky. x: its intensity; y and z: the cosine and sine of its
    // rotation about +Y; w: the highest mip level of fenrirSkySpecular, which holds roughness 1.
    vec4 skyLight;
} frame;

// The diffuse light of the sky the scene is lit by (ADR-0047): L2 spherical harmonics of its irradiance divided by
// pi, one coefficient a texel along the only row (rgb), in the cube's frame. FenrirSkyIrradiance evaluates them.
layout(set = 0, binding = 1) uniform sampler2D fenrirSkyIrradiance;
// Its specular light: its cube prefiltered with GGX, one roughness step a mip level, from 0 at mip 0 to 1 at the
// highest (frame.skyLight.w), in the cube's frame.
layout(set = 0, binding = 2) uniform samplerCube fenrirSkySpecular;
// The split sum's BRDF lookup table: red the scale and green the bias of f0, by N.V across and roughness down.
layout(set = 0, binding = 3) uniform sampler2D fenrirBrdfLut;

// The world direction `direction` turned back by a sky's rotation about +Y, whose cosine is `c` and sine `s`: the
// direction to sample that sky's cube along.
vec3 FenrirTurnedBack(vec3 direction, float c, float s)
{
    return vec3(c * direction.x - s * direction.z, direction.y, s * direction.x + c * direction.z);
}

// The direction to sample the scene's Sky cubemap along, as drawn, for the world direction `direction`.
vec3 FenrirSkyDirection(vec3 direction)
{
    return FenrirTurnedBack(direction, frame.sky.y, frame.sky.z);
}

// The direction to look up the light of the sky the scene is lit by along, for the world direction `direction`.
vec3 FenrirSkyLightDirection(vec3 direction)
{
    return FenrirTurnedBack(direction, frame.skyLight.y, frame.skyLight.z);
}

#endif
