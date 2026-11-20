# 04: A decal golden image and LightingDemo's Decals scene

Spec: [Decals](../spec.md).

**What to build:** A GL golden-image test shows a decal across a floor and a wall with angle fade, beside an object on a layer the decal skips. LightingDemo gains a Decals scene: keys spawn decals on the floor and walls, switch their layer mask, fade them and clear them, listed in the scene's help and on its demo panel.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/Tests/`, `examples/LightingDemo/`

**Status:** ready-for-agent

Stacked on 02.

- [ ] A GL golden-image test (label `gl`, skipped without a GPU) compares with a committed golden.
- [ ] The Decals scene works by hand; LightingDemo's smoke runs pass.
- [ ] LightingDemo's README says what the scene shows.
