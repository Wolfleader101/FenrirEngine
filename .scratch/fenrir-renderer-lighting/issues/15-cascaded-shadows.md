# 15: The sun casts stable cascaded shadows

**What to build:** The `Shadow` stage's first user (spec: "Shadows"; ADR-0050).

- **`DirectionalLight`'s `castShadows`** (on by default) and **`MeshRenderer`'s `castShadows`** (on by default); masked materials cast with their cutoff, blended ones never.
- **Built in two steps within the ticket:** one fitted map first, then up to four cascades per view with the practical split scheme (lambda in `RendererConfig`), boundary blending, and texel-snapped bounding spheres.
- **Filtering:** hardware comparison with Vogel-disk PCF rotated by interleaved gradient noise; slope-scaled and normal-offset biases per light.
- **Shadow passes instance**; skinned meshes cast through compute skinning; `InstancedMeshRenderer` casts by its own flag.
- **Debug views** in LightingDemo: cascade colours and the shadow map. The asteroid ring gains the planet's shadow.
- The API description gains the flags (generated output only).

**Blocked by:** 12, 14

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `examples/LightingDemo/`

**Status:** resolved

Hand check: flying around the showcase models, shadows do not shimmer or swim.

- [x] CPU unit tests: split distances; a cascade's snapped sphere does not move its texel grid as the camera moves by less than a texel.
- [x] GL pixel probes: a box's shadow on a plane is where the light says; no acne on a lit plane at grazing angles within the bias limits.
- [x] Golden images: shadows across each cascade boundary.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-11-01, stacked on ticket 14's branch (#265 to #274 not yet merged). Decisions to review:

- **One step, not two.** The single fitted map was skipped as a stepping stone: one cascade (`shadowCascades = 1`)
  is that map, through the same code.
- **A shadow atlas per camera.** `ShadowCascades`, one pass per view in the `Shadow` stage, draws each cascade into its
  tile of one transient Depth32Float texture, the cascades side by side (`shadowMapSize` 2048 each by default, so
  8192 x 2048 at four), a `RecordDraws` per tile with the tile as viewport. No texture arrays; the device gained only
  `SamplerDesc::compare` (GL's compare mode).
- **Casters run their own vertex stage** behind a new `builtin:shadow` (depth-only fragment, ALPHA_MASK variant), as the
  depth prepass does; the shadow and prepass pipelines now share `MakeBehindVertexStage`. The cascade's light matrix
  is its own view block, so any vertex stage places it right. Both faces cast. A Masked item is cut out by its prepass
  mask group; one whose shader has no base colour casts whole (the prepass cannot cut it either). Runs merge as in
  Opaque (`DrawPass::Shadow`); compute-skinned items cast from the skinned vertices.
- **Cascades** (`Core/Cascades`): the practical split (`shadowSplitLambda` 0.75), each cascade a sphere round its slice
  (radius rounded up to 1/16) whose centre is snapped to whole texels in the light's view, with a texel of margin in
  the map so the snap never pushes the slice off it. A cascade is fitted from where the previous one starts blending
  into it, so the shader's 10% blend band always has both. The sun's `shadowDistance` (100) bounds the cascades (the
  camera has no far plane) and how far towards the sun casters are caught.
- **Biases at the receiver**, in each cascade's own texels: normal offset along the face's normal (from position
  derivatives, so a normal map does not shrink it: the first run broke the normal-map probe) and slope-scaled towards
  the sun (tan clamped to 0.1..8). Nothing in the rasteriser, so a light's bias needs no pipeline. ADR-0050 is amended.
- **Reading:** set 1 binding 2 (`fenrirShadowAtlas`) and the cascades in the view block (608 bytes); `Fenrir/Shadows.glsl`
  (16 Vogel taps, 1.5 texels, IGN-rotated). Every set-1 group binds something there: a 1 x 1 "No shadow" map otherwise.
- **Not asked for:** `RendererStats::shadowDraws`; `CameraDebugView::ShadowCascades` and `ShadowAtlas` (the ticket's
  debug views, in the camera so they need no demo code); LightingDemo's "Sun shadows" panel and H key (P was taken by
  Many lights), a floor and low sun for Showcase models (the hand check needs shadows to look at). The asteroid ring's
  100k rocks take shadows but cast none (four cascades of them would cost four times the ring); the planet and the field
  cast.
- **Kept flat:** `DirectionalLight`'s shadow members sit beside `castShadows` as point and spot lights' do, not in a
  nested struct.
- **Tests:** `Tests/Cascades.cpp` (CPU), `Tests/Shadows.cpp` (null), `Tests/OpenGL/Shadows.cpp` (box shadow placement;
  no acne from 0 to 80 degrees, which fails with both biases at 0; the atlas debug view; goldens `shadow-cascades` and
  `shadow-cascades-tinted`, posts across all four cascades). `sphere-grid-gtao`'s sun casts no shadow, keeping that
  golden GTAO's.
- **msvc-x64-debug:** the whole CTest tree passed (170 tests, GL included, RTX 5080) before the review fixes, and the
  renderer's null and GL suites and the whole tree again after them.

Screenshots: `screenshots/15-cascaded-shadows/` (Showcase models lit, its cascades, its atlas; the asteroid ring with
the planet's shadow across the ring). Hand check, by the user: fly round Showcase models and watch for shimmer or swim.
