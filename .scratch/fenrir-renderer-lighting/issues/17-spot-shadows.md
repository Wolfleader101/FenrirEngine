# 17: Spot lights cast shadows from a shared atlas

**What to build:** Spot shadows within a budget (spec: "Shadows"; ADR-0050).

- **`SpotLight`'s `castShadows`**; **`RendererConfig`'s spot budget** (8 by default), filled by distance to the view each frame, the rest unshadowed and warned about once.
- **A 2D atlas** pooled by the budget, each light a tile with its own projection; the same filtering and biases as the sun.
- LightingDemo's many-lights scene gains spot shadows and an atlas debug view.

**Blocked by:** 08, 15

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `examples/LightingDemo/`

**Status:** resolved

- [x] `RendererProbe` on null: the budget picks the nearest lights; changing it resizes the atlas.
- [x] GL pixel probes: a spot shadow lands where its cone says.

Hand check: spot shadows hold as lights move in and out of the budget without popping badly.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

Built 2026-11-01, stacked on ticket 16's branch (#265 to #276 not yet merged). Decisions to review:

- **One choice a frame, not a view.** The budget ranks casting spot lights by the distance from each to the nearest
  camera of the frame (`ChooseSpotShadows`), since the frame has one spot shadow atlas. Past the budget, one warning.
- **An atlas the renderer keeps**, not a transient one: a `Depth32Float` grid of the budget's tiles (ceil(sqrt) across,
  `RendererConfig::spotShadowMapSize` 1024 each, so 3 x 3 x 1024 at the default 8), made again and the old one freed
  when `Renderer::SetSpotShadowBudget` changes the budget. Being per frame, set 0 reads it: the records
  (`GpuSpotShadow`, 96 bytes) at binding 7 and the atlas at 8. The `SpotShadows` pass binds a set 0 group with the
  "No shadow" map there, so it never reads what it draws.
- **The light buffer's order, not its record.** Shadowed spots come first in tile order; frame block `lights.y` counts
  them, so record i < `lights.y` reads spot shadow i and the 80-byte record (ticket 19's clusters) is unchanged.
- **Same machinery as the sun.** The casters, pipelines (now labelled `Shadow`), merging and compute-skinned vertices
  are ticket 15's; `Fenrir/Shadows.glsl`'s tap loop and biasing became `FenrirFilteredShadow` and `FenrirShadowBiased`,
  shared by both. A spot texel's world size grows with the distance along the light. A tile holds the outer cone (held
  to 80 degrees) with the filter's reach to spare; reads outside it are unshadowed.
- **Per-light biases.** `SpotLight` gains `shadowNormalBias` and `shadowSlopeBias`, as ADR-0050 gives each light its
  own; ADR-0050 is amended.
- **Not asked for:** `CameraDebugView::SpotShadowAtlas` (the ticket's atlas debug view, in the camera as ticket 15's);
  `RendererConfig::spotShadowMapSize`; LightingDemo's K key, cast checkbox and 0 to 16 budget slider. The demo's light
  markers now cast nothing: a sphere round a spot light shadows its whole cone, which the first screenshots showed (the
  `SpotLight` docs and README now say so).
- **Not done:** no check that the atlas fits the GPU's largest texture (a huge budget or tile size fails at creation);
  no fade as lights enter or leave the budget.
- **Tests:** `Tests/SpotShadows.cpp` (CPU grid, choice, matrix, bounds, record; null pass, tiles across rows, bindings,
  light order, camera-following budget, one warning, atlas remade and freed, debug view), `Tests/OpenGL/Shadows.cpp`
  (a box's spot shadow where the cone puts it and no acne, with the light in tile 0 and in tile 4; fails with the
  shadow multiply removed). Set 0 counts in Lights, Sky, SkyLighting, Scripting Drawing and Flattening went 7 to 9 and
  15 to 17.
- **msvc-x64-debug:** the whole CTest tree passed (170 tests, GL included).

Screenshots: `screenshots/17-spot-shadows/` (Many lights with spot shadows on and off, and the spot shadow atlas). Hand
check, by the user: in Many lights, watch lights move in and out of the budget for popping.
