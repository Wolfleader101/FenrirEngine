#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:lit (ADR-0024): metallic-roughness with GGX specular, lit by the frame's one directional light and constant
// ambient, written as linear HDR. Intensities are plain multipliers, so the BRDF is scaled by pi: a white light of
// intensity 1 falling straight onto a white rough face gives about 1, not 1 / pi.
//
// Each factor is multiplied by its texture, as glTF does: base colour by baseColourTexture, roughness by its green
// channel and metallic by the blue channel of metallicRoughnessTexture, and emissive by emissiveTexture (white by
// default, so an emissive colour shows with no texture). normalTexture is a tangent-space normal map whose green
// points up the image, in the mesh's tangent frame when it has a tangent stream (ADR-0048: glTF's are MikkTSpace) and
// otherwise in one from the screen-space derivatives of the position and UV0. Its ALPHA_MASK variant, drawn for a Masked
// material, discards a pixel whose alpha is below the material's alpha cutoff, which the renderer writes into
// `alphaCutoff`.
#include "Fenrir/Frame.glsl"
#include "Fenrir/View.glsl"

#pragma fenrir default(baseColour, 1.0, 1.0, 1.0, 1.0)
#pragma fenrir default(emissive, 0.0, 0.0, 0.0, 1.0)
#pragma fenrir default(metallic, 0.0)
#pragma fenrir default(roughness, 0.5)
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

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColour;
layout(location = 3) in vec2 vUv;
// mesh.vert's: the world tangent, and the bitangent's sign, or 0 for a mesh without tangents.
layout(location = 4) in vec4 vTangent;
layout(location = 0) out vec4 outColour;

const float Pi = 3.14159265;

// Trowbridge-Reitz (GGX) normal distribution, with a = roughness squared.
float Distribution(float nDotH, float a)
{
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (Pi * d * d);
}

// Smith's geometry term with Schlick-GGX, k for direct light.
float Geometry(float nDotV, float nDotL, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return (nDotV / (nDotV * (1.0 - k) + k)) * (nDotL / (nDotL * (1.0 - k) + k));
}

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
    vec3 v = normalize(view.cameraPosition.xyz - vWorldPosition);
    // lightDirection is the way the light travels; l points back at it.
    vec3 l = -frame.lightDirection.xyz;
    vec3 h = normalize(v + l);
    float nDotL = max(dot(n, l), 0.0);
    float nDotV = max(dot(n, v), 1e-4);
    float nDotH = max(dot(n, h), 0.0);
    float roughness = clamp(material.roughness * metallicRoughness.g, 0.04, 1.0);
    float metallic = clamp(material.metallic * metallicRoughness.b, 0.0, 1.0);

    vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    float d = Distribution(nDotH, roughness * roughness);
    float g = Geometry(nDotV, nDotL, roughness);
    vec3 specular = Pi * d * g * fresnel / (4.0 * nDotV * max(nDotL, 1e-4));
    vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base.rgb;

    vec3 colour = (diffuse + specular) * frame.lightColour.rgb * nDotL;
    colour += frame.ambient.rgb * base.rgb;
    colour += material.emissive.rgb * texture(emissiveTexture, vUv).rgb;
    outColour = vec4(colour, base.a);
}
