# 07: the renderer reports palette stats, and the Crowd scene scales the humanoid from 1 to 1024

**What to build:** the renderer's `Stats` say how much palette data each frame uploads, and AnimationDemo's scene 7 shows a crowd whose cost follows skeletons, not draws (spec: "FenrirRenderer: skinning" for `Stats`, "AnimationDemo" scene 7; user story 36).

- **`Stats`** gains `paletteBytesUploaded` (bytes copied into the frame's palette buffer), `distinctPalettesWritten` (after deduplication) and `skinnedDraws`, reset each frame across every view.
- **Scene 7, Crowd:** a grid of the humanoid, 64 to start, doubling and halving between 1 and 1024 on keys, clips and start times staggered by `Seek`, with pause and reset. Screen text shows instances, total bones, frame time and the three palette stats. No pass/fail thresholds. The README gains its row and hand checks.

**Blocked by:** 05

**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/Renderer.hpp`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Tests/`, `examples/AnimationDemo/src/`, `examples/AnimationDemo/README.md`

**Status:** resolved

- [x] Seam 1: the three counters match a known scene (two meshes and two cameras on one skeleton write one palette and count both draws) and reset each frame.
- [x] Scene 7 is in the scene list and README; smoke runs still pass.
- [ ] Hand check (left to the user as a `Hand check:` line on resolve): the crowd doubles to 1024 and halves back to 1 without a crash, clips are visibly staggered, pause and reset work, and `distinctPalettesWritten` equals the instance count.
Hand check: in scene 7 (`FENRIR_DEMO_SCENE=Crowd`, on OpenGL), 64 skater-skinned Kenney characters stand in an 8 by 8 grid, half idling and half running, visibly out of step; Up four times reaches 1024 and Down ten times halves back to 1 without a crash, the screen text's `distinctPalettesWritten` (and `skinnedDraws`) equalling the instance count at each step; Space freezes and resumes every member; R brings back 64, moving from staggered starts.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-30 on `claude/animation-06-fbx-d99563` (ticket 06, PR #242, not merged yet), as a stacked PR. Decisions to review:

- **Where the counts come from:** the core counts them in `SceneRenderer::AddPasses`, beside `draws`, and resets them there each frame: `paletteBytesUploaded` is the palette entries written times `sizeof(GpuBone)` (48), `skinnedDraws` adds each view's skinned draws (so it is part of `draws`, one per item per view), and `distinctPalettesWritten` is the new `SceneFrame::palettes`, which the facade sets from its `PaletteRanges` (one per scene, skeleton and pose). A frame with no view writes no palette, so all three are 0 then.
- **32-bit byte count:** `paletteBytesUploaded` is a `std::uint32_t` like the other counts, so `RendererStats` stays 4-byte aligned (24 bytes) with no padding in the API description; 4 GB of palette a frame is far past any crowd.
- **API description and binding regenerated** (`FenrirRenderer.api.json`, the scripting mappings and the generated `RendererStats.cs`); no hand-written C#.
- **The crowd member is Kenney's character** (58 bones, one mesh), as 06's note suggested, taking idle and run in turn, each started at the golden-ratio fraction of its clip through `Seek`. Members are placed along a Z-order curve, so doubling adds a block beside the grid and halving takes the newest half off; the grid is re-centred each time. Halving destroys the members' entity trees; R destroys all and spawns 64 again.
- **Scene order:** Crowd is the fourth scene for now; the comment in `AnimationScenes.hpp` says 08-10's scenes go before it, as the spec numbers them.
