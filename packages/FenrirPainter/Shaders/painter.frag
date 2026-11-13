#version 460
// builtin:painter, the fragment stage (ADR-0060): each quad's shape evaluated as a signed distance in its own space and
// anti-aliased over one pixel, a tessellated path's triangle covered as its vertices say, or a glyph covered as its atlas
// page says, coloured by its paint and cut by its clip. Colours arrive sRGB-encoded and premultiplied (the painter's
// vertex colours and stop texture both), and are worked in that form. The output is premultiplied
// (PremultipliedAlpha): linear by default, the target encoding it, or, in the GAMMA variant (the Gamma blend space),
// sRGB-encoded as it is, so blending happens in sRGB as a browser's does (ADR-0065).

// Set 1, the command's: no parameter block, then its image and its stop texture, white when the command binds none.
// The MASK variant, which the renderer draws a command a mask group reaches with, adds the block holding the mask's
// rectangle and the mask's image, which the renderer fills (ADR-0060).
#ifdef MASK
layout(set = 1, binding = 0, std140) uniform MaterialParams
{
    // The mask image's top-left in local units, then one over its width and height.
    vec4 maskRect;
} params;
#endif
layout(set = 1, binding = 1) uniform sampler2D image;
#pragma fenrir default(image, white)
layout(set = 1, binding = 2) uniform sampler2D stops;
#pragma fenrir default(stops, white)
#ifdef MASK
layout(set = 1, binding = 3) uniform sampler2D mask;
#pragma fenrir default(mask, white)
#endif

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vLocalAndClip;
layout(location = 2) flat in vec4 vHalfSizes;
layout(location = 3) flat in vec4 vRadii;
layout(location = 4) flat in vec4 vClipRadii;
layout(location = 5) flat in vec4 vShape;
layout(location = 6) flat in vec4 vExtents;
layout(location = 7) flat in vec4 vColour;
layout(location = 8) flat in vec4 vColour2;
layout(location = 9) flat in vec4 vBorderTop;
layout(location = 10) flat in vec4 vBorderRight;
layout(location = 11) flat in vec4 vBorderBottom;
layout(location = 12) flat in vec4 vBorderLeft;
layout(location = 13) flat in uvec4 vKinds;
#ifdef MASK
layout(location = 14) in vec2 vPosition;
#endif

layout(location = 0) out vec4 outColour;

// ShapeKind, PaintKind and PainterFlags (FenrirPainter/PainterVertex.hpp).
const uint Rectangle = 1u;
const uint RoundedRectangle = 2u;
const uint Border = 3u;
const uint Shadow = 4u;
const uint Circle = 5u;
const uint Arc = 6u;
const uint Ring = 7u;
const uint Triangle = 8u;
const uint Glyph = 9u;
const uint SdfGlyph = 10u;

const uint Solid = 0u;
const uint LinearGradient = 1u;
const uint RadialGradient = 2u;
const uint ImagePaint = 3u;

const uint Repeat = 1u;
const uint StopRow = 2u;
const uint Inset = 4u;
const uint Clipped = 8u;

vec3 Decode(vec3 srgb)
{
    return mix(srgb / 12.92, pow((srgb + 0.055) / 1.055, vec3(2.4)), greaterThan(srgb, vec3(0.04045)));
}

vec3 Encode(vec3 linear)
{
    return mix(linear * 12.92, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055, greaterThan(linear, vec3(0.0031308)));
}

// How much of a pixel `px` units wide lies inside, for a signed distance `d` (negative inside).
float Coverage(float d, float px)
{
    return clamp(0.5 - d / px, 0.0, 1.0);
}

// The signed distance to a box of half size `b` about the origin, its corners rounded by `r` (top-left, top-right,
// bottom-right, bottom-left; Y is down).
float RoundedBox(vec2 p, vec2 b, vec4 r)
{
    float radius = p.x < 0.0 ? (p.y < 0.0 ? r.x : r.w) : (p.y < 0.0 ? r.y : r.z);
    vec2 q = abs(p) - b + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

// The error function, to within 5e-4 (Abramowitz and Stegun 7.1.27).
float Erf(float x)
{
    float s = sign(x);
    float a = abs(x);
    float t = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    t *= t;
    return s - s / (t * t);
}

// A shadow's coverage: the box's edge blurred by a Gaussian of standard deviation `sigma`, which across an edge is the
// error function of the distance.
float Blurred(float d, float sigma, float px)
{
    if (sigma < px * 0.5)
    {
        return Coverage(d, px);
    }
    return 0.5 - 0.5 * Erf(d / (sigma * 1.41421356));
}

// The colour of a border at `p`: the side whose edge is nearest, measured in its own width, so the sides meet on the
// corners' diagonals.
vec4 BorderColour(vec2 p, vec2 b, vec4 widths)
{
    vec4 distances = vec4(p.y + b.y, b.x - p.x, b.y - p.y, p.x + b.x);
    vec4 measured = mix(vec4(1e9), distances / max(widths, vec4(1e-6)), greaterThan(widths, vec4(0.0)));
    float nearest = min(min(measured.x, measured.y), min(measured.z, measured.w));
    if (nearest == measured.x)
    {
        return vBorderTop;
    }
    if (nearest == measured.y)
    {
        return vBorderRight;
    }
    if (nearest == measured.z)
    {
        return vBorderBottom;
    }
    return vBorderLeft;
}

// The signed distance to a wedge of half angle `halfAngle` about +Y, of radius `r` (Inigo Quilez's pie).
float Pie(vec2 p, float halfAngle, float r)
{
    vec2 c = vec2(sin(halfAngle), cos(halfAngle));
    p.x = abs(p.x);
    float l = length(p) - r;
    float m = length(p - c * clamp(dot(p, c), 0.0, r));
    return max(l, m * sign(c.y * p.x - c.x * p.y));
}

// A gradient's colour at offset `t`, sRGB-encoded and premultiplied.
vec4 Ramp(float t, uint flags)
{
    t = (flags & Repeat) != 0u ? fract(t) : clamp(t, 0.0, 1.0);
    if ((flags & StopRow) != 0u)
    {
        // Texel x holds offset x / 255, so offsets 0 and 1 land on the first and last texel centres.
        return textureLod(stops, vec2(t * (255.0 / 256.0) + 0.5 / 256.0, vExtents.z), 0.0);
    }
    float from = vExtents.z;
    float to = vExtents.w;
    float s = to > from ? clamp((t - from) / (to - from), 0.0, 1.0) : step(from, t);
    return mix(vColour, vColour2, s);
}

// The offset of a two-point conical gradient from the focal point `f` (offset 0, radius 0) to the unit circle about the
// origin (offset 1): the t whose circle, centred at f(1 - t) with radius t, passes through `q`.
float Conical(vec2 q, vec2 f)
{
    vec2 fromFocal = q - f;
    vec2 d = -f;
    float a = dot(d, d) - 1.0;
    float b = dot(fromFocal, d);
    float c = dot(fromFocal, fromFocal);
    if (abs(a) < 1e-5)
    {
        return c / max(2.0 * b, 1e-6);
    }
    return (b - sqrt(max(b * b - a * c, 0.0))) / a;
}

void main()
{
    vec2 p = vLocalAndClip.xy;
    vec2 halfSize = vHalfSizes.xy;
    uint kind = vKinds.x;
    uint paint = vKinds.y;
    uint flags = vKinds.z;
    // A pixel's size in the shape's and the clip's spaces, and the uv's derivatives, taken here, where every pixel of
    // the quad runs.
    float px = max(0.5 * (length(dFdx(p)) + length(dFdy(p))), 1e-6);
    vec2 clipPoint = vLocalAndClip.zw;
    float clipPx = max(0.5 * (length(dFdx(clipPoint)) + length(dFdy(clipPoint))), 1e-6);
    vec2 uvDx = dFdx(vUv);
    vec2 uvDy = dFdy(vUv);

    vec4 colour;
    if (paint == LinearGradient)
    {
        colour = Ramp(vUv.x, flags);
    }
    else if (paint == RadialGradient)
    {
        colour = Ramp(Conical(vUv, vExtents.xy), flags);
    }
    else if (paint == ImagePaint)
    {
        vec4 texel = textureGrad(image, vUv, uvDx, uvDy);
        colour = vColour * vec4(Encode(texel.rgb) * texel.a, texel.a);
    }
    else
    {
        colour = vColour;
    }

    float coverage = 1.0;
    if (kind == Rectangle || kind == RoundedRectangle)
    {
        coverage = Coverage(RoundedBox(p, halfSize, vRadii), px);
    }
    else if (kind == Border)
    {
        vec4 widths = vShape;
        vec2 innerMin = -halfSize + vec2(widths.w, widths.x);
        vec2 innerMax = halfSize - vec2(widths.y, widths.z);
        vec2 innerHalf = max((innerMax - innerMin) * 0.5, vec2(0.0));
        vec4 innerRadii = max(vRadii - vec4(max(widths.w, widths.x), max(widths.x, widths.y), max(widths.y, widths.z),
                                            max(widths.z, widths.w)),
                              vec4(0.0));
        float outer = Coverage(RoundedBox(p, halfSize, vRadii), px);
        float inner = all(greaterThan(innerHalf, vec2(0.0)))
                          ? Coverage(RoundedBox(p - (innerMin + innerMax) * 0.5, innerHalf, innerRadii), px)
                          : 0.0;
        coverage = outer * (1.0 - inner);
        colour = BorderColour(p, halfSize, widths);
    }
    else if (kind == Shadow)
    {
        float sigma = max(vShape.x, 0.0) * 0.5;
        float spread = (flags & Inset) != 0u ? -vShape.y : vShape.y;
        vec2 shadowHalf = max(halfSize + spread, vec2(0.0));
        // A sharp corner stays sharp as the shadow grows or shrinks, as CSS keeps it.
        vec4 shadowRadii = mix(vec4(0.0), max(vRadii + spread, vec4(0.0)), greaterThan(vRadii, vec4(0.0)));
        float shadow = Blurred(RoundedBox(p - vShape.zw, shadowHalf, shadowRadii), sigma, px);
        float box = Coverage(RoundedBox(p, halfSize, vRadii), px);
        // Outset, outside the box only; inset, inside it only, where the moved and shrunk box does not reach.
        coverage = (flags & Inset) != 0u ? box * (1.0 - shadow) : shadow * (1.0 - box);
    }
    else if (kind == Circle)
    {
        coverage = Coverage(length(p) - halfSize.x, px);
    }
    else if (kind == Arc)
    {
        float radius = vShape.x;
        float band = abs(length(p) - radius) - vShape.y * 0.5;
        float middle = vShape.z;
        // Turned so the arc's middle points along +Y, where the pie opens.
        vec2 q = vec2(dot(p, vec2(-sin(middle), cos(middle))), dot(p, vec2(cos(middle), sin(middle))));
        coverage = Coverage(max(band, Pie(q, vShape.w, halfSize.x + px * 2.0)), px);
    }
    else if (kind == Ring)
    {
        float distance = length(p);
        coverage = Coverage(max(distance - vShape.x, vShape.y - distance), px);
    }
    else if (kind == Triangle)
    {
        // A tessellated path's triangle: its edge is already anti-aliased by the fringe the coverage fades across.
        coverage = clamp(p.x, 0.0, 1.0);
    }
    else if (kind == Glyph)
    {
        // A glyph's quad over its atlas rectangle (ADR-0062's bitmap mode): the page's R8 coverage, its texel
        // coordinates in vUv, multiplying the paint.
        vec2 texels = vec2(textureSize(image, 0));
        coverage = textureGrad(image, vUv / texels, uvDx / texels, uvDy / texels).r;
    }
    else if (kind == SdfGlyph)
    {
        // A glyph's quad over its distance field (ADR-0062's SDF mode): FreeType stores a byte v as (v - 128) / 128
        // of the spread (vShape.x, in texels) inside the outline. The texels a screen pixel spans, from vUv's
        // derivatives, take the distance to screen pixels, so the edge fades over one pixel at any scale or turn.
        vec2 texels = vec2(textureSize(image, 0));
        float field = textureGrad(image, vUv / texels, uvDx / texels, uvDy / texels).r;
        float texelDistance = (field * 255.0 - 128.0) / 128.0 * vShape.x;
        float texelsPerPixel = max(length(vec2(length(uvDx), length(uvDy))) * 0.70710678, 1e-4);
        coverage = clamp(texelDistance / texelsPerPixel + 0.5, 0.0, 1.0);
    }

    if ((flags & Clipped) != 0u)
    {
        coverage *= Coverage(RoundedBox(clipPoint, vHalfSizes.zw, vClipRadii), clipPx);
    }

    colour *= coverage;
#ifdef MASK
    // The mask's coverage: its image's alpha, nothing outside it.
    vec2 maskUv = (vPosition - params.maskRect.xy) * params.maskRect.zw;
    bool inMask = all(greaterThanEqual(maskUv, vec2(0.0))) && all(lessThanEqual(maskUv, vec2(1.0)));
    colour *= inMask ? texture(mask, maskUv).a : 0.0;
#endif
#ifdef GAMMA
    outColour = colour;
#else
    outColour = colour.a > 0.0 ? vec4(Decode(colour.rgb / colour.a) * colour.a, colour.a) : vec4(0.0);
#endif
}
