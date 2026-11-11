#version 460
#extension GL_GOOGLE_include_directive : require
// builtin:painter, the vertex stage (ADR-0060): the painter's fat vertex (FenrirPainter/PainterVertex.hpp), in the
// target's local coordinates. Everything but the position passes to the fragment stage, which evaluates the shape, its
// paint and its clip; the per-shape fields are the same at all four corners, so they pass flat.
#include "Fenrir/2D.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inLocalAndClip;
layout(location = 3) in vec4 inHalfSizes;
layout(location = 4) in vec4 inRadii;
layout(location = 5) in vec4 inClipRadii;
layout(location = 6) in vec4 inShape;
layout(location = 7) in vec4 inExtents;
layout(location = 8) in vec4 inColour;
layout(location = 9) in vec4 inColour2;
layout(location = 10) in vec4 inBorderTop;
layout(location = 11) in vec4 inBorderRight;
layout(location = 12) in vec4 inBorderBottom;
layout(location = 13) in vec4 inBorderLeft;
layout(location = 14) in vec4 inKinds;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vLocalAndClip;
layout(location = 2) flat out vec4 vHalfSizes;
layout(location = 3) flat out vec4 vRadii;
layout(location = 4) flat out vec4 vClipRadii;
layout(location = 5) flat out vec4 vShape;
layout(location = 6) flat out vec4 vExtents;
layout(location = 7) flat out vec4 vColour;
layout(location = 8) flat out vec4 vColour2;
layout(location = 9) flat out vec4 vBorderTop;
layout(location = 10) flat out vec4 vBorderRight;
layout(location = 11) flat out vec4 vBorderBottom;
layout(location = 12) flat out vec4 vBorderLeft;
layout(location = 13) flat out uvec4 vKinds;

void main()
{
    vUv = inUv;
    vLocalAndClip = inLocalAndClip;
    vHalfSizes = inHalfSizes;
    vRadii = inRadii;
    vClipRadii = inClipRadii;
    vShape = inShape;
    vExtents = inExtents;
    vColour = inColour;
    vColour2 = inColour2;
    vBorderTop = inBorderTop;
    vBorderRight = inBorderRight;
    vBorderBottom = inBorderBottom;
    vBorderLeft = inBorderLeft;
    vKinds = uvec4(round(inKinds * 255.0));
    gl_Position = target2D.localToClip * vec4(inPosition, 0.0, 1.0);
}
