# 07: Sun shafts: a golden image and the Forest clearing

Spec: [Volumetric fog](../spec.md). ADR-0088.

**What to build:** A GL golden-image test shows sun shafts through gaps in a row of pillars in fog, compared after a fixed number of frames with the fixed jitter, so the history settles the same every run. LightingDemo's Forest clearing turns volumetric fog on, with shafts between the trees, and keys and demo panel rows for on or off, quality, anisotropy and history weight, listed in the scene's help.

**Blocked by:** 03, 04, 05
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `examples/LightingDemo/`

**Status:** ready-for-agent

Top of the stack, on 05.

- [ ] A GL golden-image test (label `gl`, skipped without a GPU) compares the shafts with a committed golden.
- [ ] The Forest clearing shows shafts and its keys and rows work (a hand check, with the fetched assets); LightingDemo's smoke runs pass.
- [ ] LightingDemo's README says what the keys do.
