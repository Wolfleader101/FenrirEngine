# Volumetric fog

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [How do engines do volumetric fog?](../engine-gaps/issues/34-how-engines-do-volumetric-fog.md) and [What volumetric fog does the renderer offer?](../engine-gaps/issues/35-what-volumetric-fog-the-renderer-offers.md). Settles the volumetric half of [No fog](../engine-gaps/issues/14-fog.md). ADR-0076. Builds on [Plain fog](../fenrir-renderer-fog/spec.md), whose `Fog` resource, fog pass and fog helper it extends.

## Problem Statement

Plain fog fades the world toward the sky by distance and height, but light does not scatter in it: the sun breaking through a forest canopy makes no shafts, a lamp in mist has no glow, and fog cannot be thicker in one place than another except by height. Most engines offer volumetric fog for this, and the user wants FenrirEngine to have it as an engine feature. The renderer cannot build one today: its device has no 3D textures, no storage images and no barriers between a compute pass writing an image and a later pass reading it.

## Solution

A camera that turns volumetric fog on gets a froxel volume: a camera-aligned 3D grid filled in compute each frame with the fog's density (from the scene's `Fog` resource and from fog volume entities) and the light it scatters (the sun through its cascades, clustered point and spot lights with their shadows, and the sky), blended with the volume's own history to smooth it, and integrated front to back. The fog pass and the transparents' fog helper read it with one lookup, so opaque surfaces, transparents and particles are all fogged by it, and plain fog carries on past the volume's range. A quality setting picks the grid. The device gains 3D textures, storage images and the barriers they need. C# reaches the camera's settings and fog volumes, and LightingDemo's Forest clearing shows shafts between the trees.

## User Stories

1. As a game developer, I want to turn volumetric fog on for a camera, so that light scatters in my scene's fog.
2. As a game developer, I want it off by default, so that I pay its cost only when I ask.
3. As a game developer, I want sunlight to make shafts through gaps in trees or windows, so that my scene has depth and mood.
4. As a game developer, I want shadowed areas to stay dark in the fog, so that the shafts read against the shade.
5. As a game developer, I want point and spot lights to glow in the fog, with their shadows, so that a lamp in the mist looks lit.
6. As a game developer, I want the sky's light to fill the fog, so that it is never black where no light reaches directly.
7. As a game developer, I want a forward-scattering setting on the fog, so that it glows toward the sun as real haze does.
8. As a game developer, I want the volume to use my scene's fog density and height falloff, so that plain and volumetric fog agree.
9. As a game developer, I want fog volumes, boxes or spheres I place as entities with their own density, colour and edge falloff, so that a swamp or a cave mouth can be foggier than the rest.
10. As a game developer, I want fog volumes to move with their entities, so that drifting mist can follow a path.
11. As a game developer, I want transparents and particles fogged by the volume, so that glass and smoke sit inside the haze.
12. As a game developer, I want plain fog to carry on past the volume's range, so that distant geometry still fades.
13. As a game developer, I want to set the volume's range, so that I can place the hand-off to plain fog where it is least visible.
14. As a game developer, I want a quality setting that trades the grid's resolution for speed, so that I can fit my frame budget.
15. As a game developer, I want the volume smoothed over frames, so that it does not shimmer or band.
16. As a game developer, I want to set how much history it keeps, so that I can trade smoothness against trails behind moving lights.
17. As a game developer, I want each camera's volume and history kept apart, so that two views do not smear into each other.
18. As a game developer, I want volumetric fog to work with MSAA, so that it looks the same with antialiasing on.
19. As a C# game developer, I want to turn a camera's volumetric fog on and set its quality, range and history from C#, so that my C++ host stays a host.
20. As a C# game developer, I want to create and edit fog volumes from C#, so that my game places its own mist.
21. As an engine maintainer, I want 3D textures, storage images and image barriers in the device, so that this and later compute work can build volumes.
22. As an engine maintainer, I want the scattering maths tested on the CPU against closed forms, so that the volume's light is right.
23. As an engine maintainer, I want a golden image of sun shafts, so that a regression shows.
24. As an engine maintainer, I want LightingDemo's Forest clearing to show volumetric fog with keys to adjust it, so that I can check it by hand.
25. As an engine maintainer, I want the decision recorded in an ADR, so that a later reader knows why it is a froxel volume and how it meets plain fog.

## Implementation Decisions

- **The device** gains a 3D texture kind (width, height and depth), a storage usage on textures (read and written by compute shaders as images), and storage-image access in pass descriptors, from which the backends derive the barriers between a pass writing an image and a later pass reading or sampling it (ADR-0016, ADR-0019: barriers stay below the seam). The null device keeps their books; the OpenGL backend uses image load and store with the memory barriers they need.
- **The camera** gains volumetric fog settings: on or off (off by default), a quality (low: 16-pixel cells and 64 slices; high: 8-pixel cells and 64 slices; the spec's tickets may add a higher one), a range (the volume's far distance, default 100 units) and a history weight (default 0.9).
- **The `Fog` resource** gains an anisotropy (the Henyey-Greenstein `g`, default 0) and an albedo for scattering; its density, start distance, base height and falloff feed the volume as they feed plain fog.
- **`FogVolume`** is a component: a shape (box or sphere) on its entity's transform, a density added to the scene's, an albedo, and an edge falloff. Fog volumes are gathered per frame like lights.
- **Per camera with volumetric fog on, in compute before Opaque** (after shadows and the light clusters are ready), the renderer:
  1. fills the froxel grid with extinction and in-scattered light: density from `Fog` and the fog volumes touching each cell; light from the sun through its cascaded shadow maps, from the clustered point and spot lights with their shadows, and from the sky's spherical harmonics, scattered with the phase function;
  2. blends it with the camera's history volume, reprojected by the previous frame's view and projection, with a fixed per-frame jitter sequence inside each cell;
  3. integrates front to back into a volume of accumulated scattering and transmittance.
- **Slices** are distributed exponentially in depth from the near plane to the range, so cells are small near the camera.
- **Applying it:** the fog pass (after the background) and the fog helper in transparents' shading look up the integrated volume at each pixel's or surface's depth when the camera has it; past the range, plain fog continues from the volume's transmittance at its far end. With MSAA the fog pass runs per sample as plain fog's does. Particles are fogged through the helper.
- **The volume's textures and history** are the camera's, kept across frames, remade when the quality or view size changes (history reset then).
- **Shaders** for the new compute passes are written in Slang (ADR-0073), using the shared includes for shadows, clusters and spherical harmonics.
- **C#:** the camera's volumetric fog settings, `Fog`'s anisotropy and albedo, and the `FogVolume` component, through the generated bindings.
- **Glossary:** Fog volume is already defined; *Froxel volume* is added: a camera's 3D grid of cells along its view, holding the fog's density and scattered light.

## Testing Decisions

- **A good test here checks light and fog values, graph order and pixels**, never how a pass is coded.
- **Device tests:** null-device tests that 3D textures and storage textures are created, sized and freed, and that storage access in a pass descriptor is accepted; GL resource tests (label `gl`) where a compute pass writes a 3D storage image and a later pass reads the written values back.
- **CPU maths tests** (prior art: the BRDF and spherical-harmonics tests): the depth slice distribution and its inverse; Henyey-Greenstein normalised over the sphere; front-to-back integration of an even fog against Beer-Lambert's closed form; a fog volume's density and edge falloff; the hand-off to plain fog at the range matching plain fog alone when the volume holds only the scene's fog.
- **Null-backend renderer tests:** volumetric passes run only for cameras that turn it on, in compute before Opaque, and are read by the fog pass and the transparents' helper; the quality sets the grid's size; two cameras keep separate volumes and histories; changing quality or view size remakes the volume and resets history; fog volumes are gathered.
- **A GL golden-image test:** sun shafts through gaps in a row of pillars in fog, compared after a fixed number of frames with the fixed jitter sequence, so the history settles the same every run.
- **FenrirRendererScripting's fixture tests:** a camera's volumetric settings, `Fog`'s new fields and a `FogVolume` written from C# reach the frame.
- **By hand, LightingDemo's Forest clearing:** volumetric fog on, shafts between the trees, with keys and panel rows for on or off, quality, anisotropy and history, listed in the scene's help. It needs the fetched assets; the null and golden tests cover what its smoke run cannot.

## Out of Scope

- Fog density written by a game's own shader (fog shaders).
- Motion vectors and temporal antialiasing.
- Volumetric clouds, sky scattering and god rays as a screen-space effect.
- Local lights' volumetric shadows beyond the shadow maps they already have.
- Rect lights scattering in the volume.

## Further Notes

- Plain fog's spec must be built first: this spec extends its `Fog` resource, fog pass and fog helper.
- The facts behind the decisions, with costs from other engines, are in [How do engines do volumetric fog?](../engine-gaps/issues/34-how-engines-do-volumetric-fog.md) and its findings on the local `research/volumetric-fog` branch.
- ADR-0076 follows ADR-0075 on this branch; renumber at merge if another branch takes it.
- Build and test only `msvc-x64-debug` locally, run the GL tests on your own GPU before a PR that touches the OpenGL backend, and use the short-path junction in a worktree (`README.md`, "Building and testing").
