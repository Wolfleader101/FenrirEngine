# 05: Fog volumes

Spec: [Volumetric fog](../spec.md). ADR-0088.

**What to build:** A `FogVolume` component, a box or a sphere on its entity's transform with a density added to the scene's, an albedo and an edge falloff, makes fog thicker where it is. Fog volumes are gathered each frame like lights and added into each froxel they touch; they move with their entities.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

Stacked on 04.

- [ ] A CPU test of a fog volume's density and edge falloff for both shapes.
- [ ] Null-backend tests: fog volumes are gathered and reach the fill pass; a moved entity moves its volume.
- [ ] The API description is current with `FogVolume`.
