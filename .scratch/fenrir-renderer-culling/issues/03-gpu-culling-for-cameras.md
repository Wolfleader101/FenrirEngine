# 03: Opaque and masked draws are culled per camera on the GPU and drawn indirectly

**What to build:** The cull pass, end to end for cameras: copies of an instance list and items of a merged run that a camera cannot see are not drawn in its `DepthPrepass` or `Opaque` (spec: "Culling"; ADR-0055).

- **Candidates:** every opaque and masked run (ADR-0051's runs of merged items, instance lists, and lone items as runs of one) in a frame, less those the CPU culls for the camera first: a run, lone item or instance list whose world bounds (ticket 02's) are outside the camera's frustum writes no template and no group entries for that camera, so a chunked world's off-screen chunks cost nothing (ADR-0055 "Where the line is"). Per run that is left, the renderer writes into this frame's ring: a command template (the mesh's index count, first index 0, base vertex 0, the base instance where the run's survivors start in the view's indirection region, and no instances), and its entries in a group table, one per 64 candidates, naming the run, so no work group straddles two runs.
- **Per camera**, a compute pass in a new `Cull` stage, first in the stage list, before `Shadow`: `CmdCopyBuffer` copies the templates over the camera's command buffer, then one dispatch tests each candidate's sphere (ticket 02's bounds; an instance list's moved by the entity's world matrix and the copy's transform) against the camera's planes, and appends each survivor's `uvec2(record, copy)` to its run's region of the camera's indirection buffer, counting it into its command with `atomicAdd`. A lone or merged item's copy is `0xFFFFFFFF`.
- **`Fenrir/Draw.glsl`:** set 3 binding 3 declares the indirection entries beside the records; `CurrentDraw()` reads `fenrirVisible.entries[FenrirDrawIndex()]`, takes the entry's record, and composes the copy when there is one. The draw group's binding 3 is the camera's indirection buffer, so there is one draw group per camera per frame in flight. The shader compiler's flattening keeps the palette the third storage block.
- **`DepthPrepass` and `Opaque`** draw each run with one `DrawIndexedIndirect` of one command from the camera's command buffer, declaring `IndirectRead` on it and `StorageRead` on the indirection buffer. Both read the same result.
- **Buffers** grow to the frame's candidates (8 bytes of indirection per candidate per camera, 20 bytes of command per run per camera) and are kept between frames, ring-buffered per frame in flight like the draw buffer.
- **`RendererConfig::gpuCulling`** (on) and `Renderer::SetGpuCulling`: off, the pass accepts every candidate, so the indirection and draws are unchanged in shape. The API description gains the field (generated output only).
- `packages/FenrirRenderer/README.md` describes the stage, and what user vertex shaders may rely on (`CurrentDraw()` only, as before).

**Blocked by:** 01, 02

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tools/ShaderCompiler/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

- [ ] `RendererProbe` on null: one copy and one dispatch per camera per frame, before its prepass; the templates and group table bytes for a frame of one instance list, one merged run and one lone item; the prepass and `Opaque` record the same indirect draws, one per run, from the camera's buffer; two cameras each get their own buffers; 100k copies are still one draw per pass; an instance list wholly behind the camera writes no template and no draw for it, and one straddling the edge does.
- [ ] GL readback: for a seeded random scene of copies around and behind a camera, each run's `instanceCount` equals the count ticket 02's CPU test keeps, and the indirection holds exactly those copies.
- [ ] GL pixel probes: copies inside and straddling the frustum's edges still draw at their transforms and colours under a moved entity; with `gpuCulling` off the image is identical; a user vertex shader through `CurrentDraw()` draws its copies.

Hand check: the asteroid ring's frame time falls when the camera looks along the ring rather than across it.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
