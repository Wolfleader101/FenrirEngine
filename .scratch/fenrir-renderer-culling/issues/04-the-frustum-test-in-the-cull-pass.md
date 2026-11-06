# 04: The cull pass drops copies outside the camera, by their sphere and then their box

**What to build:** The test inside ticket 03's pass, and the switch that turns it off (spec: "Culling"; ADR-0057).

- **Two tests per candidate**, both from ticket 02's record box: the derived sphere against the camera's planes (uploaded with the pass; an instance list's box is moved by the entity's world matrix and the copy's transform, a lone or merged item's by the identity), and for a candidate that passes, the eight transformed corners, kept unless every corner is outside one plane. The shader's tests mirror `Math::Frustum`'s exactly, so the FenrirMath unit tests are the oracle.
- **`RendererConfig::gpuCulling`** (on) and `Renderer::SetGpuCulling`: off, the pass accepts every candidate, as ticket 03's did, so the indirection and draws are unchanged in shape. The API description gains the field (generated output only).

**Blocked by:** 03

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

- [ ] GL readback: for a seeded random scene of copies around and behind a camera, each run's `instanceCount` equals the count `Math::Frustum` keeps on the CPU, and the indirection holds exactly those copies, as a set; a scene of long thin copies tangent to the frustum keeps fewer than the sphere alone would.
- [ ] GL pixel probes: copies inside and straddling the frustum's edges still draw at their transforms and colours under a moved entity; with `gpuCulling` off the image is identical.
- [ ] `RendererProbe` on null: the camera's planes are uploaded with the pass each frame and change as the camera moves.

Hand check: the asteroid ring's frame time falls when the camera looks along the ring rather than across it.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
