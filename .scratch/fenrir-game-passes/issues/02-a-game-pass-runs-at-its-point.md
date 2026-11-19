# 02: A game pass runs at its point

Spec: [Game passes](../spec.md). ADR-0075.

**What to build:** A game adds a full-screen game pass from C++, naming a shader asset, one of the two points (after opaque, once the background is drawn and before transparents; or after transparents, before debug drawing, post-processing and `Output`), a blend mode into scene colour (replace, alpha or additive) and parameters set by name with the shader's defaults otherwise; it can enable, disable and remove it through the handle the renderer returns. The renderer core adds enabled passes to every camera view's render graph through a game-pass render feature, in the order added. A shared shader include gives a pass the view's matrices and size, its parameter block and a full-screen triangle vertex stage. A pass whose shader is missing or is not a game pass shader logs one error and is skipped until changed.

**Blocked by:** 01
**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`

**Status:** ready-for-agent

Stacked on 01.

- [ ] Null-backend tests: a pass at each point runs where the spec puts it, relative to the background, transparents, debug and post-processing.
- [ ] Passes at one point run in the order added; disabled and removed passes do not run.
- [ ] Parameters set by name reach the pass; unset ones take the shader's defaults.
- [ ] A missing or wrong shader logs one error and the frame still renders.
- [ ] With no game passes, the graph is unchanged.
