# 06: Outlines: a golden image and RendererDemo's Outlines scene

Spec: [Game passes](../spec.md). ADR-0087.

**What to build:** An outline game pass, reading a layer mask and scene depth, draws dark outlines around entities on an "outlined" layer and not around others. A GL golden-image test proves it on a small scene. RendererDemo gains an Outlines scene: objects on an "outlined" layer get outlines and the floor does not, with keys to change the outline's colour and width and to move an object on and off the layer, listed in the scene's help and on its demo panel.

**Blocked by:** 04
**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `examples/RendererDemo/`

**Status:** ready-for-agent

Top of the stack, on 04.

- [ ] A GL golden-image test (label `gl`, skipped without a GPU) compares a scene with one outlined and one plain entity against a committed golden.
- [ ] RendererDemo's Outlines scene shows the outlines and its keys work (a hand check); RendererDemo's smoke runs pass.
- [ ] The scene's README entry says what it shows.
