#version 460
// The BrdfLut pass (ADR-0047): the split sum's BRDF lookup table, made once at start-up. Each texel integrates GGX with
// height-correlated Smith visibility (Heitz 2014, as Filament) over the hemisphere by importance sampling, for N.V
// across (0 to 1, left to right) and perceptual roughness down (0 to 1, top to bottom), into the scale (red) and bias
// (green) the reflected f0 takes: f0 * red + green.

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec2 outScaleBias;

const float Pi = 3.14159265358979;
const uint SampleCount = 512u;

// The i-th of `count` points of the Hammersley set in [0, 1)^2.
vec2 Hammersley(uint i, uint count)
{
    return vec2(float(i) / float(count), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

// Height-correlated Smith visibility, G / (4 N.V N.L), with a = roughness squared.
float Visibility(float nDotV, float nDotL, float a)
{
    float a2 = a * a;
    float lambdaV = nDotL * sqrt((nDotV - nDotV * a2) * nDotV + a2);
    float lambdaL = nDotV * sqrt((nDotL - nDotL * a2) * nDotL + a2);
    return 0.5 / (lambdaV + lambdaL);
}

void main()
{
    // Offscreen passes put row 0 at the top (NDC +y); texel centres sample the table where its texels are.
    float nDotV = max((vNdc.x + 1.0) * 0.5, 1e-4);
    float roughness = (1.0 - vNdc.y) * 0.5;
    float a = roughness * roughness;
    vec3 v = vec3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);

    vec2 sum = vec2(0.0);
    for (uint i = 0u; i < SampleCount; ++i)
    {
        vec2 xi = Hammersley(i, SampleCount);
        float phi = 2.0 * Pi * xi.x;
        float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        vec3 h = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
        vec3 l = 2.0 * dot(v, h) * h - v;
        float nDotL = l.z;
        float nDotH = h.z;
        float vDotH = max(dot(v, h), 0.0);
        if (nDotL > 0.0)
        {
            // The sample's weight over the GGX pdf D(h) (h.n) / (4 v.h).
            float g = Visibility(nDotV, nDotL, a) * 4.0 * nDotL * vDotH / max(nDotH, 1e-4);
            float fresnel = pow(1.0 - vDotH, 5.0);
            sum += vec2((1.0 - fresnel) * g, fresnel * g);
        }
    }
    outScaleBias = sum / float(SampleCount);
}
