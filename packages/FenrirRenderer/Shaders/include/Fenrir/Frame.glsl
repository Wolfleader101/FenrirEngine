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
    // x: how many records fenrirLights.items holds this frame; y: how many of them, from the first, are spot lights
    // casting shadows (ADR-0050), record i reading fenrirSpotShadows.items[i]; z: how many point lights casting shadows
    // follow them, record y + i reading fenrirPointShadows.items[i] and cube i of fenrirPointShadowCubes. w unused.
    uvec4 lights;
} frame;

// The diffuse light of the sky the scene is lit by (ADR-0047): L2 spherical harmonics of its irradiance divided by
// pi, one coefficient a texel along the only row (rgb), in the cube's frame. FenrirSkyIrradiance evaluates them.
layout(set = 0, binding = 1) uniform sampler2D fenrirSkyIrradiance;
// Its specular light: its cube prefiltered with GGX, one roughness step a mip level, from 0 at mip 0 to 1 at the
// highest (frame.skyLight.w), in the cube's frame.
layout(set = 0, binding = 2) uniform samplerCube fenrirSkySpecular;
// The split sum's BRDF lookup table: red the scale and green the bias of f0, by N.V across and roughness down.
layout(set = 0, binding = 3) uniform sampler2D fenrirBrdfLut;

// The kinds of light a FenrirLight record is, as Core/PunctualLights' LightKind numbers them.
const uint FenrirLightPoint = 0u;
const uint FenrirLightSpot = 1u;
const uint FenrirLightRect = 2u;

// One point, spot or rect light (ADR-0048), in world space. Ticket 19's clusters index these records unchanged.
struct FenrirLight
{
    // A point or spot light's position; a rect light's centre.
    vec3 position;
    // The distance at which its light has fallen to zero.
    float range;
    // Its colour times its intensity, linear: a rect light's radiance.
    vec3 colour;
    // FenrirLightPoint, FenrirLightSpot or FenrirLightRect.
    uint kind;
    // The way a spot light points, or a rect light faces, unit length.
    vec3 direction;
    // The spot's cone falloff is smoothstep of cos(angle off its axis) x coneScale + coneOffset: for a point light,
    // scale 0 and offset 1, so 1 everywhere.
    float coneScale;
    // A rect light's right and top halves: from its centre to the middle of its right edge and of its top edge. It
    // faces the way their cross product does not.
    vec3 halfWidth;
    float coneOffset;
    vec3 halfHeight;
    // 1 for a rect light that shines from both faces.
    uint twoSided;
};

// Every point, spot and rect light of the scene that can light anything, frame.lights.x of them: the spot lights that
// cast shadows first (frame.lights.y of them), then the point lights that do (frame.lights.z), then the rest, points,
// spots and rects in that order; the buffer may be longer.
layout(set = 0, binding = 4, std430) readonly buffer FenrirLights
{
    FenrirLight items[];
} fenrirLights;

// The linearly transformed cosines of GGX (Heitz et al. 2016) that rect lights are shaded with, 64 x 64 texels each, by
// perceptual roughness across and sqrt(1 - N.V) down: the inverse transform's m00, m02, m20 and m22 (column, row), and
// in alpha of the second, the form factor of a sphere clipped by the horizon, by the cosine of its centre's elevation
// (mapped to [0, 1]) across and its unclipped form factor down. Read with FenrirLtcUv.
layout(set = 0, binding = 5) uniform sampler2D fenrirLtcMatrix;
layout(set = 0, binding = 6) uniform sampler2D fenrirLtcAmplitude;

// How a shadowed spot light's tile of fenrirSpotShadowAtlas is read (ADR-0050); Fenrir/Shadows.glsl reads it.
struct FenrirSpotShadow
{
    // World to the atlas: x and y over w are u and v (from the top), z over w the reversed-Z depth in the light's view.
    mat4 atlas;
    // x: the normal-offset bias and y: the slope-scaled bias, in texels; z: a texel's world size one unit along the
    // light; w: the filter's radius in texels.
    vec4 bias;
    // The u and v its filter may read between: its tile, less the filter's reach. xy low, zw high.
    vec4 bounds;
};

// One record per spot light casting shadows this frame, frame.lights.y of them, in the light buffer's order; the
// buffer may be longer.
layout(set = 0, binding = 7, std430) readonly buffer FenrirSpotShadows
{
    FenrirSpotShadow items[];
} fenrirSpotShadows;

// The spot lights' shadow atlas (ADR-0050): each shadowed light's reversed-Z depths in a tile of its own, compared by
// the sampler as the sun's atlas is. A 1 x 1 map of depth 0, which lights everything, when no spot light casts and in
// the pass drawing the atlas.
layout(set = 0, binding = 8) uniform sampler2DShadow fenrirSpotShadowAtlas;

// How a shadowed point light's cube of fenrirPointShadowCubes is read (ADR-0050); Fenrir/Shadows.glsl reads it.
struct FenrirPointShadow
{
    // x: the normal-offset bias and y: the slope-scaled bias, in texels; z: a texel's world size one unit along a
    // face's axis; w: the filter's radius in texels.
    vec4 bias;
};

// One record per point light casting shadows this frame, frame.lights.z of them, in the light buffer's order after the
// shadowed spot lights; the buffer may be longer.
layout(set = 0, binding = 9, std430) readonly buffer FenrirPointShadows
{
    FenrirPointShadow items[];
} fenrirPointShadows;

// The point lights' shadows (ADR-0050): a cube each, cube i belonging to fenrirPointShadows.items[i], holding the
// distance from its light to the nearest caster over the light's range, reversed (1 at the light, 0 at its range),
// compared by the sampler as the atlases are. One cube of 0, which lights everything, when no point light casts and in
// the pass drawing the cubes.
layout(set = 0, binding = 10) uniform samplerCubeArrayShadow fenrirPointShadowCubes;

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
