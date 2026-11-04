#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:reflection-trace (ADR-0056): screen-space reflections' trace, one fullscreen pass a view at its size, after
// its HiZ passes. For each pixel with depth, the camera's view of it is reflected about its prepass normal (the vertex
// normal, not a normal map's), and the mirror ray, up to the camera's max distance and cut short at the screen's
// edge, is marched through the view's Hi-Z pyramid (Uludag, "Hi-Z Screen-Space Cone-Traced Reflections", GPU Pro 5):
// down a level where the ray reaches the nearest depth of the texel it is over, up a level where it leaves the texel in
// front of it, at most ReflectionMaxIterations steps. A hit is a level-0 texel the ray reaches, or passes behind by
// less than the camera's thickness in view depth; one it passes further behind it steps over.
//
// It writes the hit's UV in last frame's view (red, green), its confidence (blue) and the ray's view-space length
// (alpha), for Fenrir/Lighting.glsl to sample last frame's colour by. The confidence is the product of the hit, the
// edge, distance and towards-camera fades, facing (0 where the hit's normal faces along the ray: the back of
// something), the history (0 when it does not hold last frame, or the hit was off last frame's screen) and the
// disocclusion: how far in front of the hit, last frame, the surface last frame's pyramid holds there was. Pixels with
// nothing drawn, or no hit, are 0.
//
// Core/ReflectionTrace mirrors every step on the CPU. Pixels count from the view's top-left corner, +y down (NDC +y
// at row 0); depth is reversed-Z, so a texel's nearest depth is its largest.
#include "Fenrir/Normals.glsl"

layout(set = 2, binding = 0) uniform sampler2D pyramid;
#ifdef MSAA
layout(set = 2, binding = 1) uniform sampler2DMS sceneNormals;
#else
layout(set = 2, binding = 1) uniform sampler2D sceneNormals;
#endif
layout(set = 2, binding = 2) uniform sampler2D previousPyramid;

layout(set = 1, binding = 0, std140) uniform ReflectionTraceData
{
    // The view's projection, and its inverse, which turns a pixel's NDC and depth back into view space.
    mat4 projection;
    mat4 inverseProjection;
    // Last frame's view-projection times this frame's inverse view: this frame's view space to last frame's clip.
    mat4 viewToPreviousClip;
    // xy: the view's size in pixels; zw: one over it.
    vec4 size;
    // x: the camera's max distance; y: its thickness; z: 1 when the history holds last frame. w unused.
    vec4 settings;
} trace;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outHit;

// As Core/ReflectionTrace's constants.
const int ReflectionMaxIterations = 64;
const float ReflectionEdgeFade = 0.1;
const float ReflectionDistanceFade = 0.2;
const float ReflectionTowardsCameraFade = 0.1;
const float CrossOffset = 0.01;
const float Never = 3.4e38;

// The view-space point at `pixel` and reversed-Z `depth`. Depth 0, an infinite far plane, is moved just off it.
vec3 ViewPositionAt(vec2 pixel, float depth)
{
    vec2 ndc = vec2(pixel.x * trace.size.z * 2.0 - 1.0, 1.0 - pixel.y * trace.size.w * 2.0);
    vec4 position = trace.inverseProjection * vec4(ndc, max(depth, 1e-7), 1.0);
    return position.xyz / position.w;
}

// The view depth (distance in front of the camera) of reversed-Z `depth`.
float ViewDepth(float depth)
{
    vec4 position = trace.inverseProjection * vec4(0.0, 0.0, max(depth, 1e-7), 1.0);
    return -position.z / position.w;
}

// The view-space point `position` on screen: pixels in xy, reversed-Z depth in z.
vec3 ProjectToScreen(vec3 position)
{
    vec4 clip = trace.projection * vec4(position, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    return vec3((ndc.x * 0.5 + 0.5) * trace.size.x, (0.5 - ndc.y * 0.5) * trace.size.y, ndc.z);
}

// The texel of `level` the pixel `pixel` is over: the last on a side takes the rest of the view, as builtin:hi-z folds.
ivec2 CellOf(vec2 pixel, int level)
{
    ivec2 size = textureSize(pyramid, level);
    return clamp(ivec2(floor(pixel / exp2(float(level)))), ivec2(0), size - 1);
}

// Where along the ray from `origin` by `along` it leaves `cell` of `level`, a little past the edge.
float ExitOf(ivec2 cell, int level, vec3 origin, vec3 along)
{
    ivec2 last = textureSize(pyramid, level) - 1;
    float side = exp2(float(level));
    vec2 t = vec2(Never);
    for (int axis = 0; axis < 2; ++axis)
    {
        float c = float(cell[axis]);
        if (along[axis] > 0.0)
        {
            float edge = cell[axis] >= last[axis] ? trace.size[axis] : (c + 1.0) * side;
            t[axis] = (edge + CrossOffset - origin[axis]) / along[axis];
        }
        else if (along[axis] < 0.0)
        {
            t[axis] = (c * side - CrossOffset - origin[axis]) / along[axis];
        }
    }
    return min(t.x, t.y);
}

// The Hi-Z march from `origin` by `along` (pixels and depth), as Core/ReflectionTrace's MarchHiZ: whether it hit, and
// how far along it stopped in `t`.
bool MarchHiZ(vec3 origin, vec3 along, float thickness, out float t)
{
    int top = textureQueryLevels(pyramid) - 1;
    // Out of the starting pixel first: its depth is the surface the ray leaves.
    t = ExitOf(CellOf(origin.xy, 0), 0, origin, along);
    int level = 0;
    for (int i = 0; i < ReflectionMaxIterations && t < 1.0; ++i)
    {
        vec3 point = origin + along * t;
        ivec2 cell = CellOf(point.xy, level);
        float nearest = texelFetch(pyramid, cell, level).r;
        float exit = ExitOf(cell, level, origin, along);
        if (point.z > nearest)
        {
            // In front of everything in the texel: on to where it reaches the texel's nearest depth, or out.
            float plane = along.z < 0.0 ? (nearest - origin.z) / along.z : Never;
            if (plane < exit)
            {
                t = plane;
                if (level == 0)
                {
                    return plane <= 1.0;
                }
                --level;
            }
            else
            {
                t = exit;
                level = min(level + 1, top);
            }
        }
        else if (level > 0)
        {
            --level;
        }
        else if (ViewDepth(point.z) - ViewDepth(nearest) <= thickness)
        {
            return true;
        }
        else
        {
            // Passed far behind it: past it at the finest level, where what is behind may be hit.
            t = exit;
        }
    }
    return false;
}

void main()
{
    outHit = vec4(0.0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(pyramid, pixel, 0).r;
    if (depth <= 0.0)
    {
        return;
    }
    vec3 position = ViewPositionAt(gl_FragCoord.xy, depth);
    vec3 normal = normalize(FenrirOctDecode(texelFetch(sceneNormals, pixel, 0).rg));
    // An orthographic camera sees every pixel along -Z.
    vec3 incident = trace.projection[3][3] > 0.5 ? vec3(0.0, 0.0, -1.0) : normalize(position);
    vec3 direction = normalize(reflect(incident, normal));
    float towardsCamera = clamp(-direction.z / ReflectionTowardsCameraFade, 0.0, 1.0);
    if (towardsCamera <= 0.0)
    {
        return;
    }

    // The ray on screen, cut short where it leaves the screen: depth is linear on screen, so at the same share.
    vec3 start = vec3(gl_FragCoord.xy, depth);
    vec3 end = ProjectToScreen(position + direction * trace.settings.x);
    vec3 along = end - start;
    float share = 1.0;
    for (int axis = 0; axis < 2; ++axis)
    {
        float edge = along[axis] > 0.0 ? trace.size[axis] : 0.0;
        if ((along[axis] > 0.0 && end[axis] > edge) || (along[axis] < 0.0 && end[axis] < edge))
        {
            share = min(share, (edge - start[axis]) / along[axis]);
        }
    }
    along *= share;

    float t;
    if (!MarchHiZ(start, along, trace.settings.y, t))
    {
        return;
    }
    vec3 hit = start + along * t;
    ivec2 hitPixel = clamp(ivec2(floor(hit.xy)), ivec2(0), ivec2(trace.size.xy) - 1);
    vec3 hitNormal = normalize(FenrirOctDecode(texelFetch(sceneNormals, hitPixel, 0).rg));
    float facing = dot(hitNormal, direction) < 0.0 ? 1.0 : 0.0;
    vec3 hitPosition = ViewPositionAt(hit.xy, hit.z);
    float rayLength = distance(position, hitPosition);

    vec2 uv = hit.xy * trace.size.zw;
    float edge = clamp(min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)) / ReflectionEdgeFade, 0.0, 1.0);
    float distanceFade =
        clamp((trace.settings.x - rayLength) / (ReflectionDistanceFade * trace.settings.x), 0.0, 1.0);

    // Where the hit was last frame, and whether that frame saw it.
    vec4 previousClip = trace.viewToPreviousClip * vec4(hitPosition, 1.0);
    vec3 previousNdc = previousClip.xyz / previousClip.w;
    vec2 previousUv = vec2(previousNdc.x * 0.5 + 0.5, 0.5 - previousNdc.y * 0.5);
    bool onScreen = all(greaterThanEqual(previousUv, vec2(0.0))) && all(lessThanEqual(previousUv, vec2(1.0)));
    float history = trace.settings.z > 0.0 && onScreen ? 1.0 : 0.0;
    ivec2 previousPixel = clamp(ivec2(previousUv * trace.size.xy), ivec2(0), ivec2(trace.size.xy) - 1);
    float previousDepth = texelFetch(previousPyramid, previousPixel, 0).r;
    float hidden = ViewDepth(previousNdc.z) - ViewDepth(previousDepth);
    float disocclusion = clamp(1.0 - hidden / trace.settings.y, 0.0, 1.0);

    float confidence = towardsCamera * facing * edge * distanceFade * history * disocclusion;
    outHit = vec4(previousUv, confidence, rayLength);
}
