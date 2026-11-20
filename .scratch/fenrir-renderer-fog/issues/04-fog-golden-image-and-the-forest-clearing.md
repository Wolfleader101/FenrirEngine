# 04: A fog golden image and the Forest clearing

Spec: [Plain fog](../spec.md).

**What to build:** A GL golden-image test shows a row of objects receding into distance and height fog under a sky, with a transparent object in front. LightingDemo's Forest clearing turns fog on, with keys and demo panel rows for distance density, height density, height falloff, the colour override and fog on or off, listed in the scene's help.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `examples/LightingDemo/`

**Status:** ready-for-agent

Top of the stack, on 02.

- [ ] A GL golden-image test (label `gl`, skipped without a GPU) compares the fogged scene with a committed golden.
- [ ] The Forest clearing shows fog and its keys and rows work (a hand check, with the fetched assets); LightingDemo's smoke runs pass.
- [ ] LightingDemo's README says what the fog keys do.
