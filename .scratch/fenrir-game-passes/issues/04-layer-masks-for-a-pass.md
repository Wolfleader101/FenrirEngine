# 04: Layer masks for a pass

Spec: [Game passes](../spec.md). ADR-0075.

**What to build:** A game pass asks for a mask of some render layers: for each distinct set of layers asked for at a point, the renderer draws the opaque and masked entities on any of those layers (instanced ones included) into a target at the view's size holding their coverage and their own depth, and binds it to the passes that asked. No mask is drawn that no pass asks for.

**Blocked by:** 01, 03
**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`

**Status:** ready-for-agent

Stacked on 03.

- [ ] Null-backend tests: a mask draws exactly the opaque and masked entities on its layers, instanced ones included, and no transparent one.
- [ ] Two passes at one point asking for the same layers share one mask; different sets get their own.
- [ ] With no pass asking, no mask is drawn.
