// Fenrir/LightRecord.glsl: one record of the frame's light buffer (ADR-0048), as Core/PunctualLights' GpuLight lays it
// out, for Fenrir/Frame.glsl's set 0 and for builtin:light-clusters, which reads the same buffer at a set of its own.
#ifndef FENRIR_LIGHT_RECORD_GLSL
#define FENRIR_LIGHT_RECORD_GLSL

// The kinds of light a FenrirLight record is, as Core/PunctualLights' LightKind numbers them.
const uint FenrirLightPoint = 0u;
const uint FenrirLightSpot = 1u;
const uint FenrirLightRect = 2u;

// One point, spot or rect light (ADR-0048), in world space. Each view's clusters (Fenrir/Clusters.glsl) index these
// records unchanged.
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

#endif
