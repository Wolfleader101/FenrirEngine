# How do engines do volumetric fog?

Type: research
Status: resolved
Blocked by: none
Settles: [No fog](14-fog.md) (volumetric fog)

## Question

How do Unreal, Godot 4 and Unity HDRP do volumetric fog, and what do Hillaire's (Frostbite, 2015) and Wronski's (Assassin's Creed IV, 2014) techniques say: froxel volume or ray march, grid resolution and depth distribution, which lights scatter (the sun through its cascades, local lights, the sky), the phase function, temporal filtering and its artefacts, how transparents and particles are fogged, how local fog volumes are placed, published costs, how it combines with plain height fog, and what each needs from a renderer (compute, 3D textures, shadow maps in compute, history)? What does FenrirRenderer have for it today, beside the plain fog [What fog does the renderer offer?](26-what-fog-the-renderer-offers.md) decided?

Findings go in `.scratch/engine-gaps/research/volumetric-fog.md` on the `research/volumetric-fog` branch.

## Answer

Researched 2026-11-19: `.scratch/engine-gaps/research/volumetric-fog.md` on the local branch `research/volumetric-fog` (c2ab65c3), never merged (`git show research/volumetric-fog:.scratch/engine-gaps/research/volumetric-fog.md`).

- **Everyone but Bevy builds a froxel volume:** Wronski (Assassin's Creed IV), Frostbite, Unreal, Godot 4 and HDRP use a camera-aligned 3D texture of cells about 8 to 16 pixels (or 64) across, with 64 to 128 depth slices packed near the camera, filled in compute: density written, then lit (the sun through its cascades, clustered local lights, the sky as spherical harmonics, a Henyey-Greenstein phase), blended with reprojected history (jittered, history weight about 0.9 to 0.95), integrated front to back, and applied with one 3D lookup per pixel, transparents included. Published costs: AC4 about 1.1 ms; Frostbite on PS4 at 900p about 2.95 ms at 8x8 tiles, 0.83 ms at 16x16; Unreal about 1 ms on PS4, about 3 ms on a GTX 970.
- **Bevy ray-marches** each fog volume's box per pixel in a post pass (Henyey-Greenstein, 64 steps, jitter left to TAA, no history of its own), so transparents are not fogged. Every froxel engine documents trails behind moving lights and volumes from its temporal filter.
- **FenrirRenderer has** compute over storage buffers, compute passes that sample textures, a froxel-shaped 16x9x24 light-cluster grid, cascades and point and spot shadows readable through includes, the sky's spherical harmonics, per-camera `SceneHistory` and `SamplePrevious`.
- **It lacks** 3D textures (`Resources.hpp:100-122`), storage images (`ShaderContainer.hpp:24-32`, ADR-0056), texture barriers (`OpenGLDevice.cpp:1330-1357`), motion vectors and TAA (ADR-0056).
- **Beside plain fog:** froxel engines feed the same height-fog density into the volume, replace its analytic in-scattering within the volume's range, and leave plain fog beyond it; Unreal says that far hand-off cannot be blended precisely. The decided plain fog (a pass after opaque plus a `Lighting.glsl` helper for transparents) has the same two places a volume is applied.
- **Sources:** Unreal's grid defaults come from a third-party wiki quoting its source, marked secondary; Hillaire's deck was read through its SlideShare text.
