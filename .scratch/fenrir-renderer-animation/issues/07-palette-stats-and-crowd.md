# 07: the renderer reports palette stats, and the Crowd scene scales the humanoid from 1 to 1024

**What to build:** the renderer's `Stats` say how much palette data each frame uploads, and AnimationDemo's scene 7 shows a crowd whose cost follows skeletons, not draws (spec: "FenrirRenderer: skinning" for `Stats`, "AnimationDemo" scene 7; user story 36).

- **`Stats`** gains `paletteBytesUploaded` (bytes copied into the frame's palette buffer), `distinctPalettesWritten` (after deduplication) and `skinnedDraws`, reset each frame across every view.
- **Scene 7, Crowd:** a grid of the humanoid, 64 to start, doubling and halving between 1 and 1024 on keys, clips and start times staggered by `Seek`, with pause and reset. Screen text shows instances, total bones, frame time and the three palette stats. No pass/fail thresholds. The README gains its row and hand checks.

**Blocked by:** 05

**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/Renderer.hpp`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Tests/`, `examples/AnimationDemo/src/`, `examples/AnimationDemo/README.md`

**Status:** ready-for-agent

- [ ] Seam 1: the three counters match a known scene (two meshes and two cameras on one skeleton write one palette and count both draws) and reset each frame.
- [ ] Scene 7 is in the scene list and README; smoke runs still pass.
- [ ] Hand check (left to the user as a `Hand check:` line on resolve): the crowd doubles to 1024 and halves back to 1 without a crash, clips are visibly staggered, pause and reset work, and `distinctPalettesWritten` equals the instance count.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
