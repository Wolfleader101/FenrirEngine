// Fenrir/Frame.glsl: binding set 0 of every material shader, the frame (ADR-0024). Written once per frame by the
// renderer, which declares the same layout in C++ and checks it against a shader's reflection in debug builds.
#ifndef FENRIR_FRAME_GLSL
#define FENRIR_FRAME_GLSL

layout(set = 0, binding = 0, std140) uniform FrameData
{
    // xyz: the way the directional light travels, in world space (its entity's world -Z), unit length. w unused.
    vec4 lightDirection;
    // rgb: the directional light's colour times its intensity, linear; black with no light. a unused.
    vec4 lightColour;
    // rgb: the ambient light's colour times its intensity, linear. a unused.
    vec4 ambient;
    // x: seconds since the app started; y: this frame's delta in seconds. zw unused.
    vec4 time;
    // The scene's Sky (ADR-0047). x: its intensity; y and z: the cosine and sine of its rotation about +Y. w unused.
    vec4 sky;
} frame;

// The direction to sample the scene's Sky cubemap along for the world direction `direction`: the sky is turned by its
// rotation, so this turns the other way.
vec3 FenrirSkyDirection(vec3 direction)
{
    float c = frame.sky.y;
    float s = frame.sky.z;
    return vec3(c * direction.x - s * direction.z, direction.y, s * direction.x + c * direction.z);
}

#endif
