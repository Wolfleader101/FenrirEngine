# What volumetric fog does the renderer offer?

Type: grilling
Status: resolved
Blocked by: 34
Settles: [No fog](14-fog.md) (volumetric fog)

## Question

The user wants volumetric fog explored as a feature of the engine, beside the plain distance and height fog [What fog does the renderer offer?](26-what-fog-the-renderer-offers.md) decided. With [How do engines do volumetric fog?](34-how-engines-do-volumetric-fog.md) in hand:

- **The technique:** a froxel volume or a ray march (the renderer map says this choice wants an ADR), its resolution and depth distribution.
- **What scatters:** the sun through its shadow cascades (shafts between trees), local lights and their shadows, the sky's light.
- **Density:** global from the `Fog` resource, height falloff, local fog volumes placed as entities, and whether a game can write density (a fog shader).
- **How it meets the rest:** the plain fog (replaced, or combined), transparents and particles, temporal filtering against the existing scene history.
- **What it needs from the device:** compute passes, 3D textures, shadow maps read in compute.
- **Its cost and a quality setting,** and whether it is per camera.
- **Where its spec sits:** with the plain fog, or as a later spec of its own.

Outcome: a spec-ready design.

## Answer

Grilled 2026-11-19 in round 5 of [Engine gaps for Flightless Frenzy](../map.md); every recommendation was accepted. It rests on [How do engines do volumetric fog?](34-how-engines-do-volumetric-fog.md). **Spec-ready, with an ADR**; its own spec, `.scratch/fenrir-renderer-volumetric-fog/`, after the plain fog's.

- **The technique:** a froxel volume built in compute, not a per-pixel ray march, so transparents and particles are fogged by it. The ADR records this and the hand-off to plain fog.
- **Device work first:** the device gains 3D textures, storage images and texture barriers as the spec's first tickets; later compute work can use them too.
- **The grid:** a volume of its own, finer than the 16x9x24 light clusters: 8- or 16-pixel cells and 64 depth slices packed near the camera, the cell size and slice count a quality setting.
- **What scatters:** the sun through its cascades, clustered point and spot lights with their shadows, and the sky's spherical harmonics, with a Henyey-Greenstein phase whose anisotropy is set on `Fog`.
- **Density:** from the `Fog` resource, with the plain fog's height falloff, plus `FogVolume` entities (a box or a sphere with density, albedo and falloff). Game-written density shaders wait.
- **Temporal filtering:** jittered and blended with its own history, reprojected with the camera's matrices (there are no motion vectors), at a weight of about 0.9; faint trails behind moving lights are accepted.
- **Beside plain fog:** applied in the same two places, the pass after opaque and the `Lighting.glsl` helper (particles included), replacing plain fog's in-scattering within the volume's range, with plain fog beyond it.
- **Switched on per camera:** opt-in with a quality setting, off by default.
