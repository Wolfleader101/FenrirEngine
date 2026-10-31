# 10: A `DepthPrepass` stage writes depth and normals before `Opaque`

**What to build:** Opaque and masked items are drawn once to depth and view-space normals, and `Opaque` shades each pixel once (spec: "The frame"; ADR-0049).

- **The stage list gains `DepthPrepass`** after `Shadow`.
- **The prepass** draws every `Opaque` and `Masked` item (alpha tested) into `SceneDepth` and a view-space normal target (RG16F, octahedral), at the view's MSAA count, through a depth-only program per vertex variant; user shaders get the same through their vertex stage.
- **`Opaque` tests depth for equality** and writes none.

**Blocked by:** 05

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`

**Status:** ready-for-agent

- [ ] `RendererProbe` on null: the prepass draws exactly the opaque and masked items, in the same order; transparent items are not in it.
- [ ] GL pixel probes: the normal target matches the lit normal; a masked quad's cut-out pixels are not in the depth.

Hand check: no scene's look changes; overdraw-heavy scenes get faster.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
