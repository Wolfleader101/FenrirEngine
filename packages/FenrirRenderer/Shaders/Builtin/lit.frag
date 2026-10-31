#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:lit (ADR-0024, ADR-0048): metallic-roughness with GGX specular, lit through Fenrir/Lighting.glsl by the
// frame's directional light and the scene's Sky, written as linear HDR. It is the include's first user: it fills a
// FenrirSurface from its parameters and textures and returns FenrirLitColour of it.
//
// Each factor is multiplied by its texture, as glTF does: base colour by baseColourTexture, roughness by its green
// channel and metallic by the blue channel of metallicRoughnessTexture, and emissive by emissiveTexture (white by
// default, so an emissive colour shows with no texture). normalTexture is a tangent-space normal map whose green
// points up the image, in the mesh's tangent frame when it has a tangent stream (ADR-0048: glTF's are MikkTSpace) and
// otherwise in one from the screen-space derivatives of the position and UV0. occlusionTexture's red channel (glTF's)
// is how much of the sky reaches the pixel, white by default; it darkens the sky's light only. emissiveStrength
// multiplies the emissive colour past 1 (glTF's KHR_materials_emissive_strength). Its ALPHA_MASK variant, drawn for a
// Masked material, discards a pixel whose alpha is below the material's alpha cutoff, which the renderer writes into
// `alphaCutoff`.
#include "Fenrir/Lighting.glsl"

#pragma fenrir default(baseColour, 1.0, 1.0, 1.0, 1.0)
#pragma fenrir default(emissive, 0.0, 0.0, 0.0, 1.0)
#pragma fenrir default(metallic, 0.0)
#pragma fenrir default(roughness, 0.5)
#pragma fenrir default(emissiveStrength, 1.0)
#ifdef ALPHA_MASK
#pragma fenrir default(alphaCutoff, 0.5)
#endif
layout(set = 2, binding = 0, std140) uniform MaterialParams
{
    // Linear RGBA.
    vec4 baseColour;
    // Linear RGB added to the lit colour; alpha unused.
    vec4 emissive;
    float metallic;
    float roughness;
    // Multiplies emissive, so it can pass 1.
    float emissiveStrength;
#ifdef ALPHA_MASK
    // MaterialData::alphaCutoff, written by the renderer over any override.
    float alphaCutoff;
#endif
} material;
#pragma fenrir default(baseColourTexture, white)
layout(set = 2, binding = 1) uniform sampler2D baseColourTexture;
#pragma fenrir default(metallicRoughnessTexture, white)
layout(set = 2, binding = 2) uniform sampler2D metallicRoughnessTexture;
#pragma fenrir default(normalTexture, normal)
layout(set = 2, binding = 3) uniform sampler2D normalTexture;
#pragma fenrir default(emissiveTexture, white)
layout(set = 2, binding = 4) uniform sampler2D emissiveTexture;
#pragma fenrir default(occlusionTexture, white)
layout(set = 2, binding = 5) uniform sampler2D occlusionTexture;

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColour;
layout(location = 3) in vec2 vUv;
// mesh.vert's: the world tangent, and the bitangent's sign, or 0 for a mesh without tangents.
layout(location = 4) in vec4 vTangent;
layout(location = 0) out vec4 outColour;

// The tangent frame of the mesh's tangent stream at this pixel: the interpolated tangent Gram-Schmidt'd against n, and
// the bitangent cross(n, t) times its sign, as glTF gives it. False when the mesh has no tangents (a sign of 0), or the
// tangent is degenerate.
bool StreamFrame(vec3 n, out mat3 frame)
{
    vec3 t = vTangent.xyz - n * dot(n, vTangent.xyz);
    if (abs(vTangent.w) < 0.5 || dot(t, t) < 1e-12)
    {
        return false;
    }
    t = normalize(t);
    frame = mat3(t, cross(n, t) * (vTangent.w < 0.0 ? -1.0 : 1.0), n);
    return true;
}

// A tangent frame from the derivatives of the position and UV0 (Schueler's cotangent frame), keeping the sign of its
// determinant, which Schueler drops: it is negative when screen y runs down, as it does in the renderer's offscreen
// targets, and dropping it would turn the map half a turn. False with no UV variation (a mesh without UV0 reads UV 0
// everywhere).
bool DerivativeFrame(vec3 n, out mat3 frame)
{
    vec3 dpx = dFdx(vWorldPosition);
    vec3 dpy = dFdy(vWorldPosition);
    vec2 duvx = dFdx(vUv);
    vec2 duvy = dFdy(vUv);
    vec3 dpyPerp = cross(dpy, n);
    vec3 dpxPerp = cross(n, dpx);
    vec3 t = dpyPerp * duvx.x + dpxPerp * duvy.x;
    // UV0's v grows down the image, and the map's green points up it.
    vec3 b = -(dpyPerp * duvx.y + dpxPerp * duvy.y);
    float scale = max(dot(t, t), dot(b, b));
    if (scale < 1e-20)
    {
        return false;
    }
    float inverse = (dot(dpx, dpyPerp) < 0.0 ? -1.0 : 1.0) * inversesqrt(scale);
    frame = mat3(t * inverse, b * inverse, n);
    return true;
}

// The surface normal n, facing the triangle's front, with normalTexture applied in the mesh's tangent frame, or the
// derivative frame when the mesh has no tangents. With neither frame, n is used as it is.
vec3 MappedNormal(vec3 n)
{
    mat3 frame;
    if (!StreamFrame(n, frame) && !DerivativeFrame(n, frame))
    {
        return n;
    }
    vec3 mapped = texture(normalTexture, vUv).xyz * 2.0 - 1.0;
    return normalize(frame * mapped);
}

void main()
{
    vec4 base = material.baseColour * texture(baseColourTexture, vUv) * vColour;
#ifdef ALPHA_MASK
    if (base.a < material.alphaCutoff)
    {
        discard;
    }
#endif
    vec4 metallicRoughness = texture(metallicRoughnessTexture, vUv);
    vec3 n = MappedNormal(normalize(vNormal));
    // A double-sided material's back faces are lit as their own side: the whole frame turned over.
    if (!gl_FrontFacing)
    {
        n = -n;
    }

    FenrirSurface surface;
    surface.baseColour = base.rgb;
    surface.alpha = base.a;
    surface.normal = n;
    surface.position = vWorldPosition;
    surface.metallic = material.metallic * metallicRoughness.b;
    surface.roughness = material.roughness * metallicRoughness.g;
    surface.occlusion = texture(occlusionTexture, vUv).r;
    surface.emissive = material.emissive.rgb * material.emissiveStrength * texture(emissiveTexture, vUv).rgb;
    outColour = FenrirLitColour(surface);
}
