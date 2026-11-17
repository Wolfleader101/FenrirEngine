# 11: LightingDemo's large scenes on the kit

**What to build:** Sponza, Neon city, Asteroid ring and Forest clearing move their ImGui sections onto the kit, their stats lines as readouts. LightingDemo then sets no `imguiOverlay`.

**Spec:** `.scratch/demo-ui/spec.md`, "Per-demo moves" (LightingDemo). The inventory is `.scratch/demo-ui/research/inventory.md`, "LightingDemo", these scenes.

- **Sponza:**
  - frame and draw stats through `frameStats`, or readouts
  - vsync, sun intensity, torches lit, torch shadows, flicker, torch intensity and torch range
- **Neon city:**
  - readouts for the frame, light counts, buildings, panels and cars, draws and instances
  - clustered, vsync, traffic, puddles 0..1, reflections
  - the lamp, sign, headlight and uplight intensities
- **Asteroid ring:**
  - rocks as an integer range; automatic instancing and ring turning
  - readouts for rocks, lists, draws, instances, drawn, uploads and the frame
  - a "Culling" separator: GPU, occlusion and freeze, the level-of-detail error in pixels (log)
  - readouts for drawn by level of detail (eight values) and by phase, and its note as `Text`
- **Forest clearing:**
  - density as an integer range, and grass
  - readouts for copies, lists and trees, blades and chunks, draws and the frame
- **Each scene:** its key rows. The fetched-assets message (`FetchedAssets.cpp`) stays a message.
- **`main.cpp`** drops `imguiOverlay = true`. No ImGui call or include remains in `examples/LightingDemo/`.
- **README:** "The demo panel" covers these scenes. Their hand checks are rewritten, and no ImGui mention is left.

**Blocked by:** 10

**Touches:** `examples/LightingDemo/`

**Status:** resolved

- [x] No ImGui call or include remains in `examples/LightingDemo/`, and `main.cpp` sets no `imguiOverlay`.
- [x] `LightingDemo-Smoke-null` passes with scripting off and on.
- [ ] Hand checks, on OpenGL, with the fetched assets:
  - each scene's rows work as their ImGui ones did
  - its readouts match what the ImGui text showed
  - the asteroid ring's culling switches and its by-level-of-detail readouts change together
  - the panel costs no visible frame time in Neon city or the asteroid ring
- [ ] Clang and MSVC x64 debug build without new warnings.
