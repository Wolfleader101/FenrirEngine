# 04: The cull pass drops copies outside the camera, by their sphere and then their box

**What to build:** The test inside ticket 03's pass, and the switch that turns it off (spec: "Culling"; ADR-0057).

- **Two tests per candidate**, both from ticket 02's record box: the derived sphere against the camera's planes (uploaded with the pass; an instance list's box is moved by the entity's world matrix and the copy's transform, a lone or merged item's by the identity), and for a candidate that passes, the eight transformed corners, kept unless every corner is outside one plane. The shader's tests mirror `Math::Frustum`'s exactly, so the FenrirMath unit tests are the oracle.
- **The coarse cull on the CPU** (spec "Culling", ADR-0057): a lone item, a merged run or an instance list whose world bounds (`ResolvedDraw::worldBounds`, ticket 02) are outside a view's `Math::Frustum` is left out of that view's candidates before the pass, as transparent items already are; it is the switch below's other half, so a view with `gpuCulling` off still culls by entity.
- **`RendererConfig::gpuCulling`** (on) and `Renderer::SetGpuCulling`: off, the pass accepts every candidate, as ticket 03's did, so the indirection and draws are unchanged in shape. The API description gains the field (generated output only).

**Blocked by:** 03

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/api/`

**Status:** resolved

- [x] GL readback: for a seeded random scene of copies around and behind a camera, each run's `instanceCount` equals the count `Math::Frustum` keeps on the CPU, and the indirection holds exactly those copies, as a set; a scene of long thin copies tangent to the frustum keeps fewer than the sphere alone would.
- [x] GL pixel probes: copies inside and straddling the frustum's edges still draw at their transforms and colours under a moved entity; with `gpuCulling` off the image is identical.
- [x] `RendererProbe` on null: the camera's planes are uploaded with the pass each frame and change as the camera moves.

Hand check: the asteroid ring's frame time falls when the camera looks along the ring rather than across it.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-11-10 (culling 04). Decisions the ticket left open: the job block grows to 112 bytes, `uvec4(firstGroup, groupCount, planeCount, 0)` then `vec4 planes[6]`, the count 0 with `gpuCulling` off so the pass keeps everything without a variant; the shader reads the copies from set 1 binding 4 (the instance-list buffer, which the pass now declares `StorageRead` on) and tests a list copy under `world * copy.transform`, an item under the identity, the sphere then the corners in `Math::Corners` order; the coarse CPU cull sits where the transparent one did, so one `Intersects` per draw per view drops it from both lists and from the `instances` stat. The API description gains no field: `RendererConfig` is not a described type (ticket 02 found the same); it and the C# bindings were regenerated for `InstancedMeshRenderer`'s doc comment, which no longer says copies are never culled. Eight existing null tests had opaque scenes outside the null camera's view and are now in it (`Instancing.cpp`'s rows along -Z, `Culling.cpp`'s opaque cubes dropped like the transparent ones and its record-bounds list at x = -3, `SpotShadows.cpp`'s moved camera turned back to the cube, the C# fixture's camera at (1, 2, 5)).
