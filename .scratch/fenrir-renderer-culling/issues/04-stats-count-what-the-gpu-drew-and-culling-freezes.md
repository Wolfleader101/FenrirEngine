# 04: `RendererStats` count what the GPU drew, and culling can be frozen to look at

**What to build:** The two things a person needs to see culling working: how many copies survived, and where the frustum was (spec: "Culling"; ADR-0055).

- **A delayed readback** in the graphics device: a buffer usage for readback and `GraphicsDevice::ReadBuffer(buffer, offset, bytes)`, allowed only once the frame that wrote the buffer has left flight (`FramesInFlight` frames later), so it never stalls. GL reads with `glGetNamedBufferSubData`; null returns what was last written by `WriteBuffer` or copied in (zeros for what only a dispatch wrote).
- **Each camera's command buffer is copied** at the end of its frame into a readback ring; the renderer reads the oldest finished copy each frame.
- **`RendererStats` gains `instancesDrawn`** (copies and items drawn after culling, summed over cameras) **and `framesLate`**, how old that count is; `instances` stays the candidates submitted. Each field's comment says which is which.
- **`Renderer::SetCullingFrozen(bool)`**: frozen, each camera keeps culling against the planes and position it had when frozen while it draws from where it is now, so flying out shows what was kept. The debug feature draws the frozen frustum's edges in `Debug`.
- The API description gains the fields and the function (generated output only).

**Blocked by:** 03

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

- [ ] `RendererProbe` on null: `ReadBuffer` before the writing frame leaves flight fails and reads nothing; after, it returns the written bytes; each camera's command buffer is copied into the readback ring once a frame.
- [ ] GL: `instancesDrawn` for the seeded scene of ticket 03 equals the CPU count, `framesLate` frames after it was drawn.
- [ ] `RendererProbe` on null: while frozen, the cull pass's uploaded planes stay those of the frozen camera as the camera moves, and the view's matrices do not; unfreezing restores them.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
