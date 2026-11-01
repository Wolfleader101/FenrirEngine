# 10: A `DepthPrepass` stage writes depth and normals before `Opaque`

**What to build:** Opaque and masked items are drawn once to depth and view-space normals, and `Opaque` shades each pixel once (spec: "The frame"; ADR-0049).

- **The stage list gains `DepthPrepass`** after `Shadow`.
- **The prepass** draws every `Opaque` and `Masked` item (alpha tested) into `SceneDepth` and a view-space normal target (RG16F, octahedral), at the view's MSAA count, through a depth-only program per vertex variant; user shaders get the same through their vertex stage.
- **`Opaque` tests depth for equality** and writes none.

**Blocked by:** 05

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`

**Status:** resolved

- [x] `RendererProbe` on null: the prepass draws exactly the opaque and masked items, in the same order; transparent items are not in it.
- [x] GL pixel probes: the normal target matches the lit normal; a masked quad's cut-out pixels are not in the depth.

Hand check: no scene's look changes; overdraw-heavy scenes get faster.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-11-01, stacked on ticket 09's branch (#265 to #268 not yet merged). Decisions to review:

- **Stage order** is ADR-0049's with ticket 03's `Prepare` first: `Prepare, Shadow, DepthPrepass, Opaque, ...`.
- **The program per item:** each prepass pipeline runs the item's own resolved vertex stage (base, `SKINNED`, the fallback's) behind `builtin:depth-prepass`'s fragment stage, a variant made by pairing the two stages. User shaders need nothing new; the fragment stage reads `mesh.vert`'s outputs (world normal at location 1, vertex colour at 2 and UV0 at 3 for masked items). `Fenrir/Vertex.glsl` now declares `invariant gl_Position`, so both programs place each vertex alike for the equal test.
- **Masked cut-outs** follow builtin:lit's and builtin:unlit's rule, as Unity's default depth pass does with `_BaseMap` and `_Cutoff`: `baseColour` alpha x `baseColourTexture` alpha x vertex alpha against the cutoff. A masked material's prepass set 2 is its own group (a 16-byte block of alpha and cutoff, and the base colour texture, with a target image replaced as ADR-0026 replaces it in that view), so it works whatever the shader's block layout. A Masked material whose shader has neither `baseColour` nor `baseColourTexture`, or an item whose prepass pipeline cannot be made (a vertex stage the fragment stage cannot link with, logged once as a warning), leaves the prepass and draws in `Opaque` testing greater-or-equal and writing depth, so it is never lost; it is missing from `SceneNormals`. `Opaque` therefore loads depth writable rather than read-only.
- **The normal is the geometric one**, the interpolated vertex normal turned over for back faces, in view space; normal maps are not applied in the prepass (GTAO, its reader, wants the geometric normal; applying a material's map would need its own fragment stage). The GL probe checks it against what builtin:lit shades with on a surface without a map.
- **`SceneNormals`** is RG16F at the view's size and MSAA count, cleared to (0, 0), which unpacks to view-space +Z. `DrawnView::sceneNormals` hands it to later features (ticket 11). `Fenrir/Normals.glsl` packs and unpacks, with the CPU mirror `Core/OctahedralNormals`.
- **`CompareOp::Equal`** added to the device (GL_EQUAL).
- **Tests:** `Tests/DepthPrepass.cpp` (null: the prepass draws exactly the opaque and masked items in Opaque's order, none transparent, fallback included; prepass pipelines write greater-or-equal, Opaque tests Equal without writing; the targets and load ops; a masked item's prepass block and texture; a masked user shader with no base colour draws in Opaque writing depth; a skinned item's prepass runs SKINNED), `Tests/OctahedralNormals.cpp` (round trips, the cleared value), `Tests/OpenGL/DepthPrepass.cpp` (the normal target holds the lit normal for turned quads and cameras; a double-sided back face's turned normal; a masked quad's cut-out pixels show and hold the normal of the quad behind). A mutation that drew masked items uncut in the prepass failed both cut-out GL tests. Pass-list and depth-state expectations in Bloom, Drawing, Materials and RendererFrames updated. Every golden image still matches.
- **Hand check left to the user:** no scene's look changes (the goldens and smoke runs agree); overdraw-heavy scenes get faster (not measured here).
