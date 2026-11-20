---
status: proposed
---

# Volumetric fog is a per-camera froxel volume built in compute, handing off to plain fog at its range

Plain fog (`.scratch/fenrir-renderer-fog/spec.md`) fades the world toward the sky but cannot show light scattering through it: no shafts where the sun breaks through trees, no glow round a lamp in the mist. The renderer map left "a froxel volume or a half-resolution ray march" as a choice wanting an ADR. Volumetric fog is a **froxel volume**: a camera-aligned 3D grid of cells, filled in compute with the fog's density and the light it scatters, filtered against its own history, integrated front to back, and read with one lookup per pixel by the same two places plain fog is applied. It was settled in a grilling session on 2026-11-19 (`.scratch/engine-gaps/issues/35-what-volumetric-fog-the-renderer-offers.md`, findings in `research/volumetric-fog`; spec `.scratch/fenrir-renderer-volumetric-fog/spec.md`).

## The decision

- **A froxel volume, per camera, opt-in.** Each camera that turns volumetric fog on gets its own volume and history; it is off by default, since it costs milliseconds a game should choose to spend.
- **Built in compute, before Opaque:** density from the scene's `Fog` resource and its fog volumes; lighting from the sun through its cascades, clustered point and spot lights with their shadows, and the sky's spherical harmonics, with a Henyey-Greenstein phase; a jittered blend with the camera's own history, reprojected by the previous frame's camera matrices (there are no motion vectors); then integration front to back.
- **Its own grid,** 8- or 16-pixel cells and 64 or more depth slices packed toward the camera, finer than the light clusters, chosen by a quality setting.
- **Read where plain fog is read:** the fog pass after the background and the shared fog helper in transparents' shading (particles included), so transparents are fogged by the volume too.
- **Plain fog beyond its range:** within the volume's range the volume replaces plain fog's in-scattering and extinction; past it, plain fog continues from the volume's transmittance at its far end.
- **The device gains** 3D textures, storage images and the barriers between writing and reading them, which later compute work can use too.

## Considered options

- **A per-pixel ray march** (Bevy's). Rejected: it fogs no transparents without a second march, needs its own denoising, and costs per pixel what a froxel grid costs per cell.
- **Reusing the light clusters' grid** (16 x 9 x 24). Rejected: too coarse for shafts.
- **One volume shared by every camera.** Rejected: a froxel grid is camera-aligned, and history is per view.

## Consequences

- Moving lights and fog volumes leave faint trails from the history blend; this is accepted, as every froxel engine documents it.
- The hand-off at the volume's far end cannot be blended exactly (Unreal says the same); the range is the game's to set so the seam sits where the fog is thin or the view is blocked.
- Its compute shaders are written in Slang (ADR-0073).
