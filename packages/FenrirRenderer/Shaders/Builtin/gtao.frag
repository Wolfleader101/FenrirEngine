#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:gtao (ADR-0049): ground-truth ambient occlusion (Jimenez et al. 2016, "Practical Real-Time Strategies for
// Accurate Indirect Occlusion"), after Intel's XeGTAO (MIT; Resources/XeGTAO/LICENSE.txt), from a view's depth prepass.
// Two passes a view, both full-screen at the view's size:
//
// - The base variant, GtaoHorizons: for each pixel, the horizon search of XeGTAO's main pass over its depth and normal,
//   GtaoSlices slices of GtaoSteps steps each way, rotated and jittered per pixel by XeGTAO's Hilbert-curve R2 noise,
//   and integrated against the cosine-weighted hemisphere about the normal: how much of the sky reaches the pixel,
//   0 to 1, raised to the camera's power. With the MSAA key, depth and normals are multisampled and sample 0 is read.
//   Its second output is XeGTAO's packed edges: how far each of the four neighbours lies off the pixel's surface.
// - The DENOISE variant, GtaoDenoise, twice (XeGTAO's two-pass setting, as there is no temporal filter): its 3 x 3
//   edge-aware blur of the occlusion, weighting each neighbour by the edges between them, so occlusion does not bleed
//   across silhouettes; the first pass with a fifth of the final pass's centre weight, so it blurs more.
//
// The view space is the renderer's (right-handed, the camera looking down -Z, reversed-Z depth, NDC +y at row 0), not
// XeGTAO's (left-handed, +Z forward); the search uses only dot products and lengths, so it carries over as it is.
// XeGTAO's depth MIP chain and temporal noise are left out: depth is read at full resolution, and the noise is fixed.
#include "Fenrir/Constants.glsl"
#include "Fenrir/Normals.glsl"

layout(set = 1, binding = 0, std140) uniform GtaoData
{
    // The view's projection, and its inverse, which turns a pixel's NDC and depth back into view space.
    mat4 projection;
    mat4 inverseProjection;
    // xy: the view's size in pixels; zw: one over it.
    vec4 size;
    // x: the effect radius in view-space units (the camera's radius times XeGTAO's 1.457); y: the power the occlusion
    // is raised to (XeGTAO's 2.2 times the camera's intensity); z: a denoise pass's centre weight (XeGTAO's blur beta,
    // 1.2 for the final pass and a fifth of it before). w unused.
    vec4 settings;
} gtao;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outOcclusion;

const float GtaoHalfPi = 0.5 * FenrirPi;

// A pixel's coordinates clamped into the view, as XeGTAO's clamping sampler reads past its edges.
ivec2 GtaoClamped(ivec2 pixel)
{
    return clamp(pixel, ivec2(0), ivec2(gtao.size.xy) - 1);
}

#ifdef DENOISE
// The first pass's occlusion, and its packed edges.
layout(set = 2, binding = 0) uniform sampler2D rawOcclusion;
layout(set = 2, binding = 1) uniform sampler2D edges;

// XeGTAO_UnpackEdges: left, right, top and bottom, each 0 (an edge) to 1 (the same surface), from two bits apiece.
vec4 UnpackEdges(ivec2 pixel)
{
    uint packed = uint(texelFetch(edges, GtaoClamped(pixel), 0).r * 255.0 + 0.5);
    return vec4(float((packed >> 6u) & 3u), float((packed >> 4u) & 3u), float((packed >> 2u) & 3u),
                float(packed & 3u)) /
           3.0;
}

float Occlusion(ivec2 pixel)
{
    return texelFetch(rawOcclusion, GtaoClamped(pixel), 0).r;
}

// XeGTAO_Denoise for one pixel: the centre weighted by the pass's blur beta, each side neighbour by the edge between
// them (both ways), and each corner by the edges along its two paths, with a little leaking where the pixel is edged on
// three or four sides.
void main()
{
    float blurBeta = gtao.settings.z;
    const float diagonalWeight = 0.85 * 0.5;
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 edgesL = UnpackEdges(pixel + ivec2(-1, 0));
    vec4 edgesR = UnpackEdges(pixel + ivec2(1, 0));
    vec4 edgesT = UnpackEdges(pixel + ivec2(0, -1));
    vec4 edgesB = UnpackEdges(pixel + ivec2(0, 1));
    vec4 edgesC = UnpackEdges(pixel) * vec4(edgesL.y, edgesR.x, edgesT.w, edgesB.z);
    const float leakThreshold = 2.5;
    const float leakStrength = 0.5;
    float edginess = clamp(4.0 - leakThreshold - dot(edgesC, vec4(1.0)), 0.0, 1.0) / (4.0 - leakThreshold) *
                     leakStrength;
    edgesC = clamp(edgesC + edginess, 0.0, 1.0);

    float weightTL = diagonalWeight * (edgesC.x * edgesL.z + edgesC.z * edgesT.x);
    float weightTR = diagonalWeight * (edgesC.z * edgesT.y + edgesC.y * edgesR.z);
    float weightBL = diagonalWeight * (edgesC.w * edgesB.x + edgesC.x * edgesL.w);
    float weightBR = diagonalWeight * (edgesC.y * edgesR.w + edgesC.w * edgesB.y);

    float sum = Occlusion(pixel) * blurBeta;
    float weights = blurBeta;
    sum += Occlusion(pixel + ivec2(-1, 0)) * edgesC.x;
    sum += Occlusion(pixel + ivec2(1, 0)) * edgesC.y;
    sum += Occlusion(pixel + ivec2(0, -1)) * edgesC.z;
    sum += Occlusion(pixel + ivec2(0, 1)) * edgesC.w;
    sum += Occlusion(pixel + ivec2(-1, -1)) * weightTL;
    sum += Occlusion(pixel + ivec2(1, -1)) * weightTR;
    sum += Occlusion(pixel + ivec2(-1, 1)) * weightBL;
    sum += Occlusion(pixel + ivec2(1, 1)) * weightBR;
    weights += edgesC.x + edgesC.y + edgesC.z + edgesC.w + weightTL + weightTR + weightBL + weightBR;
    outOcclusion = vec4(sum / weights, 0.0, 0.0, 1.0);
}

#else
#ifdef MSAA
layout(set = 2, binding = 0) uniform sampler2DMS sceneDepth;
layout(set = 2, binding = 1) uniform sampler2DMS sceneNormals;
#else
layout(set = 2, binding = 0) uniform sampler2D sceneDepth;
layout(set = 2, binding = 1) uniform sampler2D sceneNormals;
#endif
layout(location = 1) out vec4 outEdges;

// XeGTAO's High preset (3 slices of 3 steps) and a slice more, since there is no temporal filter to gather more.
const int GtaoSlices = 4;
const int GtaoSteps = 3;

// The depth (reversed-Z, 0 the far plane) at a pixel: sample 0 of a multisampled target, as texelFetch's last
// argument is the sample there and the level here.
float DepthAt(ivec2 pixel)
{
    return texelFetch(sceneDepth, GtaoClamped(pixel), 0).r;
}

// The view-space point at a pixel's centre and depth. Depth 0, nothing drawn, is moved off the far plane, which an
// infinite perspective puts at infinity.
vec3 ViewPosition(vec2 pixelCentre, float depth)
{
    vec2 ndc = vec2(pixelCentre.x * gtao.size.z * 2.0 - 1.0, 1.0 - pixelCentre.y * gtao.size.w * 2.0);
    vec4 position = gtao.inverseProjection * vec4(ndc, max(depth, 1e-7), 1.0);
    return position.xyz / position.w;
}

// The distance in front of the camera of the surface at a pixel.
float ViewDepthAt(ivec2 pixel)
{
    ivec2 at = GtaoClamped(pixel);
    return -ViewPosition(vec2(at) + 0.5, DepthAt(at)).z;
}

// XeGTAO_CalculateEdges: 1 where a neighbour lies on the pixel's surface (allowing for its slope), falling to 0 as it
// leaves it by more than about 1% of the pixel's depth.
vec4 CalculateEdges(float centre, float left, float right, float top, float bottom)
{
    vec4 edgesLRTB = vec4(left, right, top, bottom) - centre;
    float slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
    float slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
    vec4 slopeAdjusted = edgesLRTB + vec4(slopeLR, -slopeLR, slopeTB, -slopeTB);
    edgesLRTB = min(abs(edgesLRTB), abs(slopeAdjusted));
    return clamp(1.25 - edgesLRTB / (centre * 0.011), 0.0, 1.0);
}

// XeGTAO_PackEdges: two bits apiece, left in the highest.
float PackEdges(vec4 edgesLRTB)
{
    edgesLRTB = round(clamp(edgesLRTB, 0.0, 1.0) * 2.9);
    return dot(edgesLRTB, vec4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

// XeGTAO_FastACos (Eberly's fit).
float FastACos(float x)
{
    float a = abs(x);
    float result = (-0.156583 * a + GtaoHalfPi) * sqrt(1.0 - a);
    return x >= 0.0 ? result : FenrirPi - result;
}

// XeGTAO's HilbertIndex: a pixel's place along a Hilbert curve over 64 x 64 tiles.
uint HilbertIndex(uint x, uint y)
{
    uint index = 0u;
    for (uint level = 32u; level > 0u; level /= 2u)
    {
        uint regionX = (x & level) > 0u ? 1u : 0u;
        uint regionY = (y & level) > 0u ? 1u : 0u;
        index += level * level * ((3u * regionX) ^ regionY);
        if (regionY == 0u)
        {
            if (regionX == 1u)
            {
                x = 63u - x;
                y = 63u - y;
            }
            uint swap = x;
            x = y;
            y = swap;
        }
    }
    return index;
}

// XeGTAO's spatio-temporal noise at frame 0: the R2 sequence along the Hilbert curve, for the slices' turn and the
// steps' jitter.
vec2 Noise(ivec2 pixel)
{
    uint index = HilbertIndex(uint(pixel.x) % 64u, uint(pixel.y) % 64u);
    return fract(0.5 + float(index) * vec2(0.75487766624669276005, 0.5698402909980532659114));
}

// XeGTAO_MainPass, without its depth MIP chain, its debug drawing or bent normals.
void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = DepthAt(pixel);
    vec2 pixelCentre = vec2(pixel) + 0.5;
    vec3 centre = ViewPosition(pixelCentre, depth);
    float viewDepth = -centre.z;
    outEdges = vec4(PackEdges(CalculateEdges(viewDepth, ViewDepthAt(pixel + ivec2(-1, 0)),
                                             ViewDepthAt(pixel + ivec2(1, 0)), ViewDepthAt(pixel + ivec2(0, -1)),
                                             ViewDepthAt(pixel + ivec2(0, 1)))),
                    0.0, 0.0, 1.0);
    // Nothing drawn here: the sky, unoccluded.
    if (depth <= 0.0)
    {
        outOcclusion = vec4(1.0);
        return;
    }

    vec3 normal = FenrirOctDecode(texelFetch(sceneNormals, pixel, 0).rg);
    // Moved a little towards the camera against the depth buffer's imprecision, as XeGTAO does for 32-bit depth.
    centre *= 0.99999;
    // An orthographic view (no perspective divide) looks along -Z everywhere.
    bool orthographic = gtao.projection[2][3] == 0.0;
    vec3 viewVec = orthographic ? vec3(0.0, 0.0, 1.0) : normalize(-centre);

    const float falloffShare = 0.615;
    const float sampleDistributionPower = 2.0;
    const float pixelTooCloseThreshold = 1.3;
    float effectRadius = gtao.settings.x;
    float falloffRange = falloffShare * effectRadius;
    float falloffFrom = effectRadius * (1.0 - falloffShare);
    float falloffMul = -1.0 / falloffRange;
    float falloffAdd = falloffFrom / falloffRange + 1.0;

    // The effect radius in pixels at this depth: the radius projected, in place of XeGTAO's pixel size at the depth,
    // so orthographic views work too.
    vec4 projectedCentre = gtao.projection * vec4(centre, 1.0);
    vec4 projectedEdge = gtao.projection * vec4(centre + vec3(effectRadius, 0.0, 0.0), 1.0);
    float screenRadius =
        abs(projectedEdge.x / projectedEdge.w - projectedCentre.x / projectedCentre.w) * 0.5 * gtao.size.x;

    vec2 noise = Noise(pixel);
    // Faded out where the radius covers few pixels.
    float visibility = clamp((10.0 - screenRadius) / 100.0, 0.0, 1.0) * 0.5;
    // Where sampling starts, so the centre pixel is never sampled.
    float minS = pixelTooCloseThreshold / screenRadius;
    for (int slice = 0; slice < GtaoSlices; ++slice)
    {
        float phi = (float(slice) + noise.x) / float(GtaoSlices) * FenrirPi;
        float cosPhi = cos(phi);
        float sinPhi = sin(phi);
        // In pixels, y down the view.
        vec2 omega = vec2(cosPhi, -sinPhi) * screenRadius;
        vec3 directionVec = vec3(cosPhi, sinPhi, 0.0);
        vec3 orthoDirectionVec = directionVec - dot(directionVec, viewVec) * viewVec;
        vec3 axisVec = normalize(cross(orthoDirectionVec, viewVec));
        vec3 projectedNormalVec = normal - axisVec * dot(normal, axisVec);
        float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
        float projectedNormalVecLength = length(projectedNormalVec);
        float cosNorm = clamp(dot(projectedNormalVec, viewVec) / projectedNormalVecLength, 0.0, 1.0);
        float n = signNorm * FastACos(cosNorm);
        float lowHorizonCos0 = cos(n + GtaoHalfPi);
        float lowHorizonCos1 = cos(n - GtaoHalfPi);
        float horizonCos0 = lowHorizonCos0;
        float horizonCos1 = lowHorizonCos1;
        for (int step = 0; step < GtaoSteps; ++step)
        {
            // The R1 sequence along the steps.
            float stepNoise = fract(noise.y + float(slice + step * GtaoSteps) * 0.6180339887498948482);
            float s = (float(step) + stepNoise) / float(GtaoSteps);
            s = pow(s, sampleDistributionPower) + minS;
            // Snapped to pixel centres.
            ivec2 offset = ivec2(round(s * omega));
            ivec2 pixel0 = GtaoClamped(pixel + offset);
            ivec2 pixel1 = GtaoClamped(pixel - offset);
            vec3 delta0 = ViewPosition(vec2(pixel0) + 0.5, DepthAt(pixel0)) - centre;
            vec3 delta1 = ViewPosition(vec2(pixel1) + 0.5, DepthAt(pixel1)) - centre;
            float distance0 = length(delta0);
            float distance1 = length(delta1);
            float weight0 = clamp(distance0 * falloffMul + falloffAdd, 0.0, 1.0);
            float weight1 = clamp(distance1 * falloffMul + falloffAdd, 0.0, 1.0);
            float shc0 = mix(lowHorizonCos0, dot(delta0 / max(distance0, 1e-12), viewVec), weight0);
            float shc1 = mix(lowHorizonCos1, dot(delta1 / max(distance1, 1e-12), viewVec), weight1);
            horizonCos0 = max(horizonCos0, shc0);
            horizonCos1 = max(horizonCos1, shc1);
        }
        // XeGTAO's fudge against slight over-darkening on steep slopes.
        projectedNormalVecLength = mix(projectedNormalVecLength, 1.0, 0.05);
        float h0 = -FastACos(horizonCos1);
        float h1 = FastACos(horizonCos0);
        float iarc0 = (cosNorm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
        float iarc1 = (cosNorm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
        visibility += projectedNormalVecLength * (iarc0 + iarc1);
    }
    visibility /= float(GtaoSlices);
    visibility = pow(clamp(visibility, 0.0, 1.0), gtao.settings.y);
    // Never wholly occluded: the pixel is seen, after all.
    outOcclusion = vec4(max(0.03, visibility), 0.0, 0.0, 1.0);
}
#endif
