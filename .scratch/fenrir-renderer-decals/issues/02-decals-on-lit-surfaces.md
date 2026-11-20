# 02: Decals on lit surfaces

Spec: [Decals](../spec.md).

**What to build:** A `Decal` component (a decal material; the entity's unit cube projecting along its local -Z; opacity; angle fade; distance fade; an optional lifetime and fade-out; a sort order; `layers`, default every layer) marks the surfaces inside its box. Decals are gathered each frame like lights, written into a decal buffer up to a per-view cap in the renderer config (farthest, then oldest, dropped past it), and listed in each light cluster's cell they touch by the clustering compute. The shared lighting function, before lighting, applies each decal in the pixel's cluster whose layers include the drawn item's (carried from the draw record to the fragment stage): colour by alpha, normal by a reoriented blend, roughness and metal by alpha, emission added, in sort order. Built-in lit materials and any material shading through the function take decals; unlit materials, transparents, shadows and depth do not change. A decal past its lifetime stops drawing and is reported ended.

**Blocked by:** 01
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`

**Status:** ready-for-agent

Stacked on 01. Needs the game passes spec's render layers (`.scratch/fenrir-game-passes/issues/01`) built first.

- [ ] CPU maths tests: projection into a decal's box and atlas region; angle and distance fades; the normal blend; sort order of several decals on one pixel.
- [ ] Null-backend tests: decals are listed in the cells they touch; the cap drops the farthest, then the oldest; layers reach the shading; a decal with a lifetime ends.
- [ ] With no decals the frame is unchanged.
- [ ] The API description is current with `Decal`.
