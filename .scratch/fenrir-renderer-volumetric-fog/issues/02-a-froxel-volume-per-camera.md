# 02: A froxel volume per camera

Spec: [Volumetric fog](../spec.md). ADR-0076.

**What to build:** A camera that turns volumetric fog on (its settings: on or off, off by default; quality, low with 16-pixel cells or high with 8-pixel cells, 64 slices; range, default 100 units) gets its own froxel volume, slices spread exponentially in depth from the near plane to the range, kept across frames and remade when its quality or view size changes. In compute before Opaque the renderer fills it with extinction and in-scattered light from the scene's `Fog` density, lit for now only by the sky's spherical harmonics, and integrates it front to back. The fog pass and the transparents' fog helper (so particles too) look up the integrated volume for that camera, and past the range plain fog continues from the volume's transmittance at its far end. Its compute shaders are written in Slang (ADR-0073).

**Blocked by:** 01
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

Stacked on 01. Needs the plain fog spec built first (`.scratch/fenrir-renderer-fog/`): it extends that spec's `Fog` resource, fog pass and fog helper.

- [ ] CPU maths tests: the slice distribution and its inverse; front-to-back integration of an even fog against Beer-Lambert's closed form; the hand-off at the range matching plain fog alone when the volume holds only the scene's fog.
- [ ] Null-backend tests: volumetric passes run only for cameras that turn it on, in compute before Opaque, and the fog pass and helper read the volume.
- [ ] Two cameras keep separate volumes; quality sets the grid's size; changing quality or view size remakes the volume.
- [ ] With no camera turning it on, the frame is unchanged.
