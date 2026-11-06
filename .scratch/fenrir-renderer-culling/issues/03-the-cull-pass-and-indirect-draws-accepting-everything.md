# 03: A cull pass per camera writes the indirection, and `DepthPrepass` and `Opaque` draw indirectly from it

**What to build:** The whole GPU-driven path with no test in it yet: every candidate survives, so the frame looks exactly as before while the plumbing is proven (spec: "Culling"; ADR-0057). Ticket 04 adds the frustum test.

- **Candidates:** every opaque and masked run (ADR-0051's runs of merged items, instance lists, and lone items as runs of one) in a frame, less those the CPU culls for the camera first: a run, lone item or instance list whose world bounds (ticket 02's) fail the camera's box test writes nothing for that camera, so a chunked world's off-screen chunks cost nothing (ADR-0057 "Where the line is"). Per run that is left, the renderer writes into this frame's ring: a command template (the mesh's index count, first index 0, base vertex 0, the base instance where the run's survivors start in the view's indirection region, and no instances), and its entries in a group table, one per 64 candidates, naming the run, so no work group straddles two runs.
- **Per camera**, a compute pass in a new `Cull` stage, first in the stage list, before `Shadow`, open to user passes like the others: `CmdCopyBuffer` copies the templates over the camera's command buffer, then one dispatch appends each candidate's `uvec2(record, copy)` to its run's region of the camera's indirection buffer, counting it into its command with `atomicAdd`. A lone or merged item's copy is `0xFFFFFFFF`. This ticket's shader accepts every candidate.
- **`Fenrir/Draw.glsl`:** set 3 binding 3 declares the indirection entries beside the records; `CurrentDraw()` reads `fenrirVisible.entries[FenrirDrawIndex()]`, takes the entry's record, and composes the copy when there is one. The draw group's binding 3 is the camera's indirection buffer, so there is one draw group per camera per frame in flight. The shader compiler's flattening keeps the palette the third storage block.
- **`DepthPrepass` and `Opaque`** draw each run with one `DrawIndexedIndirect` of one command from the camera's command buffer, declaring `IndirectRead` on it and `StorageRead` on the indirection buffer. Both read the same result.
- **Buffers** grow to the frame's candidates (8 bytes of indirection per candidate per camera, 20 bytes of command per run per camera) and are kept between frames, ring-buffered per frame in flight like the draw buffer.
- `packages/FenrirRenderer/README.md` describes the stage, and what user vertex shaders may rely on (`CurrentDraw()` only, as before).

**Blocked by:** 01, 02

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tools/ShaderCompiler/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`

**Status:** ready-for-agent

- [ ] `RendererProbe` on null: one copy and one dispatch per camera per frame, in `Cull` before `Shadow`; the templates and group table bytes for a frame of one instance list, one merged run and one lone item; the prepass and `Opaque` record the same indirect draws, one per run, from the camera's buffer; two cameras each get their own buffers; 100k copies are still one draw per pass; an instance list wholly behind the camera writes no template and no draw for it, and one straddling the edge does.
- [ ] GL readback: every candidate's `(record, copy)` is in the indirection, as a set, and each run's `instanceCount` is its candidate count.
- [ ] GL pixel probes and golden images: every existing scene is unchanged, copies included, under a moved entity and with per-instance colour; a user vertex shader through `CurrentDraw()` draws its copies.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
