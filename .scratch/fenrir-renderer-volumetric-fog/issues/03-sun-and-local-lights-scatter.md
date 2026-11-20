# 03: Sun and local lights scatter

Spec: [Volumetric fog](../spec.md). ADR-0076.

**What to build:** The sun scatters in the volume through its cascaded shadow maps, and the clustered point and spot lights through their shadows, with a Henyey-Greenstein phase. `Fog` gains an anisotropy (the phase's `g`, default 0) and an albedo for scattering. Shafts appear where light gets through gaps and shade stays dark.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

Stacked on 02.

- [ ] A CPU test: the Henyey-Greenstein phase is normalised over the sphere for several `g`.
- [ ] Null-backend tests: the volume's fill pass binds the cascades, the light clusters and the local shadow maps when volumetric fog is on.
- [ ] The API description is current with `Fog`'s new fields.
