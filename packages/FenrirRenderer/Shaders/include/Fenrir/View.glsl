// Fenrir/View.glsl: binding set 1 of every material shader, the view (ADR-0024): one camera drawing one viewport.
// Depth is reversed-Z in [0, 1] with an infinite far plane, and clip space has an upper-left origin (ADR-0016,
// ADR-0026). The renderer declares the same layout in C++ and checks it against a shader's reflection in debug builds.
#ifndef FENRIR_VIEW_GLSL
#define FENRIR_VIEW_GLSL

layout(set = 1, binding = 0, std140) uniform ViewData
{
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    // xyz: the camera's world position. w unused.
    vec4 cameraPosition;
    // The viewport in pixels of its target: x, y (top left), width, height.
    vec4 viewport;
    // The sun's cascaded shadow (ADR-0050), which Fenrir/Shadows.glsl reads. Cascade i takes world positions to u and
    // v (from the top) in fenrirShadowAtlas and their reversed-Z depth in its light's view.
    mat4 shadowMatrices[4];
    // The view depth each cascade reaches to, nearest first.
    vec4 shadowSplits;
    // The world size of each cascade's texels.
    vec4 shadowTexels;
    // x: the normal-offset bias and y: the slope-scaled bias, both in the cascade's texels; z: the filter's radius in
    // texels; w: the share of a cascade's depth range, at its far end, that blends into the next.
    vec4 shadowBias;
    // x: how many cascades there are, 0 for no shadow; y: 1 to tint each pixel by its cascade (a camera's
    // CameraDebugView::ShadowCascades). zw unused.
    uvec4 shadowInfo;
    // The view's light clusters (ADR-0049, Fenrir/Clusters.glsl). x: the view depth its first slice starts at; y:
    // 24 / ln(far / near), which takes the log of a depth over x to its slice. zw unused.
    vec4 clusterDepths;
    // x: 1 when fenrirClusterLights holds the view's clusters, 0 to loop over every light; y: 1 to show each pixel's
    // light count as heat (CameraDebugView::LightClusters); z: how many indices each cluster's list has room for.
    // w unused.
    uvec4 clusterInfo;
} view;

// The view's ambient occlusion (ADR-0049) at its pixels, as its GTAO passes make it from the depth prepass: in red, how
// much of the sky reaches each pixel's nearest opaque surface, 0 to 1. The 1 x 1 white texture when the view's camera
// has ambient occlusion off, and in every pass but Opaque, so transparent items are not darkened by what is behind them.
layout(set = 1, binding = 1) uniform sampler2D fenrirAmbientOcclusion;

// The sun's shadow atlas (ADR-0050): each cascade's reversed-Z depths, side by side, compared by the sampler (lit where
// the reference is at least the stored depth) and filtered bilinearly. A 1 x 1 map of depth 0, which lights
// everything, where the view has no shadow and in the shadow passes themselves.
layout(set = 1, binding = 2) uniform sampler2DShadow fenrirShadowAtlas;

// The view's light clusters (ADR-0049), as its LightClusters pass lists them: each cluster's light count, by
// FenrirClusterIndex, then each cluster's light buffer indices, ascending, view.clusterInfo.z words a cluster. A single
// 0 where the view loops over every light instead (view.clusterInfo.x 0).
layout(set = 1, binding = 3, std430) readonly buffer FenrirClusterLights
{
    uint words[];
} fenrirClusterLights;

#endif
