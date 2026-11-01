# 16: The forest clearing at golden hour

**What to build:** The dense, alpha-tested, animated instancing case (spec: "LightingDemo").

- **Scene 6:** Poly Haven CC0 trees, ferns, shrubs, rocks and logs (fetched) as about 20k instances over several instance lists; a Poly Haven ground set and a sunset panorama; a low sun with cascades.
- **About 200k procedural grass blades** (a generated mesh, no asset) through a **user wind shader** in the demo, swaying in its vertex stage and lighting through `Fenrir/Lighting.glsl`, also used by the depth prepass and shadow passes.
- **Alpha-tested leaves** exercise `Masked` in the prepass and in shadows; GTAO grounds the plants.

**Blocked by:** 11, 13, 15

**Touches:** `examples/LightingDemo/`

**Status:** resolved

- [x] LightingDemo's smoke runs pass on null with no fetched assets, the grass drawn from its generated mesh.

Hand check: the grass sways and its shadows sway with it; leaves cut out cleanly in light and shadow.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-11-01, stacked on ticket 15's branch (#265 to #275 not yet merged). The first version (Poly Haven saplings
round a flat clearing of instanced grass patches) was redirected by the user towards a temperate European forest: a
40-metre clearing ringed by dense conifers and broadleaves, a large mossy boulder in the foreground, a fallen log, a
winding dirt path, dense ferns and grass, a sunlit patch in the middle and a darker forest round it, in Release. Built
with what is fetchable (CC0, Poly Haven); the rest deferred, at the user's choice, to tickets 23 (debug views) and 24
(haze and sun shafts), and Fab (Megascans) trees and plants, which need a login, noted here. Decisions to review:

- **Trees.** Poly Haven's only tall CC0 conifer is Fir Tree 01 (487 MB, 2 to 4 million twig triangles a tree); Pine Tree
  01 (958 MB) is left out. The renderer has no LODs, so the demo makes them at spawn: `ThinnedCards` keeps one card in
  16, 48 or 128 of a tree's foliage by distance past the clearing's edge, each grown by about the square root of what it
  stands for, and simplifies the farther trunks by vertex clustering (`Simplified`, moved from the asteroid ring into
  `src/MeshTools.cpp` with `FirstCopies` and `Placement`; it smears bark across UV seams, so the nearest stay whole).
  About 1,170 trees.
- **Shadow budget.** Every cascade draws every caster, so the deepest trees, the far understory and pebbles cast
  nothing (deep trees darker instead, the floor under them too, through the ground mesh's canopy channel), and the
  understory grows only near the clearing. 30 ms a frame in Release on an RTX 5080 (1280 x 720, 4x MSAA); the first
  layout's 7,000 ferns through the whole forest cost 45. GPU culling and LODs are ticket-planned elsewhere (PR #273).
- **Alpha.** Poly Haven's JPEG glTFs drop the leaves' and twigs' alpha, so the cards draw solid whatever the material
  says (MASK for the ferns and shrubs, BLEND for the trees, OPAQUE for the saplings): the
  demo fetches each species' alpha map and merges it into its base colour, drawn `Masked` at cutoff 0.3 with the alpha
  strengthened 1.6x, since thin needles average away in the smaller mips. This is what makes the ticket's "alpha-tested
  leaves" true; the first version drew solid cards.
- **Ground and grass.** A generated terrain (a hollow, the path worn lower) under a second user shader blending three
  Poly Haven sets by per-vertex weights (eight textures a set at most, so the soil has no ARM map). Grass blades are
  generated per 8-metre chunk (38 meshes, MeshRenderers, not instance lists, since each follows the terrain and the
  path), two-sided in the mesh so the material is single-sided: the prepass's normals and the lit pass agree (review of
  the first version found back faces disagreeing).
- **Sun** from the panorama's brightest texel (Qwantani Late Afternoon, 19 degrees up); the tall trees leave a gap
  towards it; the demo's own sky puts its sun there too.
- **Not asked for:** the density (N) and grass (M) controls, the frame time, the stand-ins without fetched assets, and
  the demo's own late-afternoon sky (the scene's sky without the fetched panorama, S to switch).
- **Known gap, an engine one:** the glTF importer reads neither `KHR_texture_transform` nor a texture's `texCoord`, so
  Fir Tree 01's bark and `trunk_c` and Tree Small 02's branches sample their textures at the wrong scale. Worth a
  ticket of its own for the asset importer.
- **About 20k copies**, half of them unshadowed pebbles; ADR-0052 amended for the fetched assets' size.

Tests: the scene's smoke runs on null and OpenGL with no fetched assets (the grass and ground from their generated
meshes, both user shaders through prepass and shadows). Hand checks by the user: the grass and its shadows sway; leaves
and twigs cut out in light and shadow. Screenshots in `screenshots/16-forest-clearing/` (Release).
