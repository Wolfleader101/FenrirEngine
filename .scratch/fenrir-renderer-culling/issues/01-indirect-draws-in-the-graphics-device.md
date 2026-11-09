# 01: The graphics device draws indexed indirect, many commands at once

**What to build:** Indirect draws through the private graphics device, the backends and the command list, for the cull pass to feed (spec: "Culling"; ADR-0057, ADR-0016).

- **`BufferUsage::Indirect`** and **`ResourceUsage::IndirectRead`** in `packages/FenrirRenderer/src/Device/`. A pass that draws from a buffer declares `IndirectRead` on it, as it declares every other use.
- **`CmdDrawIndexedIndirect { buffer, offset, drawCount, stride }`** in `Command` and `PassEncoder::DrawIndexedIndirect`. Each command in the buffer is five `u32`s, `indexCount, instanceCount, firstIndex, baseVertex, baseInstance`, as GL's `DrawElementsIndirectCommand` and Vulkan's `VkDrawIndexedIndirectCommand` both lay it; a header names the struct (`DrawIndexedIndirectCommand`) and asserts its size.
- **The GL backend** binds the buffer to `GL_DRAW_INDIRECT_BUFFER` and replays with `glMultiDrawElementsIndirect`, the bound index buffer's offset folded into each command's first index as `CmdDrawIndexed` does. Where a pass's declared uses read a buffer (`IndirectRead`, `StorageRead`, `Vertex`/`Index` reads) that an earlier pass wrote as `StorageWrite`, it issues the matching `glMemoryBarrier` bits (`GL_COMMAND_BARRIER_BIT` for indirect reads), extending what lighting ticket 14 derived for compute skinning.
- **The null backend** records the command and checks it: the buffer is live and has `Indirect` usage, the pass declared `IndirectRead` on it, and `offset + drawCount * stride` fits. A failed check is a `StaleUses` line, as for stale handles, and draws nothing.
- `Fenrir/Draw.glsl`'s `FenrirDrawIndex()` holds for indirect draws unchanged (GL's base instance comes from each command); a comment says so.

**Blocked by:** none (the spec's Prerequisite: lighting tickets thirteen and fourteen merged)

**Touches:** `packages/FenrirRenderer/src/Device/`, `packages/FenrirRenderer/src/Backend/`, `packages/FenrirRenderer/Tests/`

**Status:** resolved

- [x] `RendererProbe` on null: a recorded indirect draw is in `Submitted` with its fields; a buffer without `Indirect` usage, an undeclared `IndirectRead`, and a range past the buffer's end each give a `StaleUses` line.
- [x] GL pixel probes: one multi-draw of two commands from a CPU-written buffer draws both meshes at their base instances' records; a command with zero instances draws nothing.
- [x] GL pixel probe: a compute dispatch that writes `instanceCount` before a pass that draws from it draws that many (the barrier holds).

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

- Built on branch `claude/culling-01-indirect-draws-e084e3`. One contract the ticket under-specified: GL cannot fold the bound index buffer's offset into the GPU's commands (there is no element-buffer offset), so an indirect draw's index buffer is bound at its start on every backend; the GL backend refuses an offset with an error and the null backend with a `StaleUses` line. The barriers are derived at each pass's begin from its declared uses over the buffers earlier passes wrote as storage, replacing the unconditional barrier at a compute pass's end.
