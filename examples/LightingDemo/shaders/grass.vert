#version 460
#extension GL_GOOGLE_include_directive : require
// LightingDemo's grass, vertex stage: a user material shader (the forest clearing) that sways each blade in the wind.
// The renderer runs this stage in every pass the grass draws in, the depth prepass and the sun's shadow cascades too,
// so the blades' depth, their shadows and their lit pixels all sway together; it writes what builtin:depth-prepass reads
// (the world normal at location 1).
//
// The grass mesh's blades stand on y 0 of their patch, so a vertex's own height is how far up its blade it is: the
// wind moves it by the square of that height, the root not at all and the tip most, and lowers it a little so the
// blade keeps its length as it bends. The wind is a slow gust travelling along the wind's direction with a faster
// flutter across it, both read at the vertex's world position on the ground, so neighbouring blades sway alike and
// distant ones out of step, and frame.time drives it.
#include "Fenrir/Draw.glsl"
#include "Fenrir/Frame.glsl"
#include "Fenrir/Vertex.glsl"
#include "Fenrir/View.glsl"

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vColour;
layout(location = 3) out vec2 vUv;

// Where the wind blows, across the ground, unit length.
const vec2 WindDirection = vec2(0.8, 0.6);
// How far a blade's tip a metre up would move in the strongest gust, in metres; the blades are under a metre tall.
const float WindStrength = 0.55;

// How far the wind moves a point a metre up its blade at `ground`, at `seconds`, across the ground.
vec2 Wind(vec2 ground, float seconds)
{
    // A broad band of gust rolling along the wind every few seconds, never quite still between gusts.
    float along = dot(ground, WindDirection);
    float gust = 0.5 + 0.5 * sin(along * 0.18 - seconds * 1.1);
    gust = 0.25 + 0.75 * gust * gust;
    // Blade-scale flutter, two waves at odd angles so it does not read as stripes.
    float flutter = sin(dot(ground, vec2(1.9, 2.7)) + seconds * 3.3) * 0.6 +
                    sin(dot(ground, vec2(-3.1, 1.3)) + seconds * 4.9) * 0.4;
    vec2 across = vec2(-WindDirection.y, WindDirection.x);
    return WindStrength * (WindDirection * gust + across * flutter * 0.25 * gust);
}

void main()
{
    DrawItem item = CurrentDraw();
    vec4 world = item.world * vec4(inPosition, 1.0);
    // The vertex's height up its blade, in the patch's metres before the copy's scale.
    float height = max(inPosition.y, 0.0);
    vec2 sway = Wind(world.xz, frame.time.x) * height * height;
    world.xz += sway;
    world.y -= 0.5 * dot(sway, sway) / max(height, 0.05);

    vWorldPosition = world.xyz;
    vNormal = mat3(item.normal) * inNormal;
    vColour = inColour * item.colour;
    vUv = inUv0;
    gl_Position = view.viewProjection * world;
}
