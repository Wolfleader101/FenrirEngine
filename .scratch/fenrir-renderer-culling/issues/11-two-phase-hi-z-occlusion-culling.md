# 11: Copies hidden behind others are culled against the depth pyramid, in two phases

**What to build:** Occlusion culling for cameras, on by default (spec: "Occlusion"; ADR-0057 "Occlusion").

- **Persistent visibility:** one bit per candidate per camera, kept between frames while the frame's candidates keep their order (reset when they change).
- **Phase one:** ticket 04's cull, but only candidates visible last frame are drawn into `DepthPrepass`.
- **The pyramid** is the one lighting 29 (ADR-0056) builds for reflections, shared: per camera, RG32F, nearest and farthest depth of each texel's footprint, built by fragment passes into its mips (the device has no storage images), the odd row and column folded in, MSAA resolved by the nearest and farthest sample. A camera gets one when either reflections or occlusion wants it. Whichever of lighting 29 and this ticket lands first builds the passes, and the other adds its channel or its second build; this ticket adds the farthest-depth channel and a build after phase one's prepass draws.
- **Phase two:** every candidate not drawn in phase one and inside the frustum is tested by its box's screen rectangle against the pyramid level whose texels cover it in at most 2x2, reading the farthest depth; those not wholly behind it are drawn into the prepass after phase one's. Visibility bits are rewritten from both phases. `Opaque` draws both phases' survivors, and the pyramid is built again from the complete depth for GTAO and reflections, as lighting 29 placed it.
- **`RendererConfig::occlusionCulling`** (on), the API description gaining it (generated output only); `RendererStats` counts each phase's draws.
- **Shadow views do not occlusion cull.** The README says why.

**Blocked by:** 08

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

- [ ] CPU unit tests: a box's screen rectangle and pyramid level under a perspective view; the test keeps a box that straddles the near plane; the pyramid's level sizes for even and odd views, and the reduction keeps the farthest depth when a level's size is odd.
- [ ] `RendererProbe` on null: two cull dispatches and two pyramid builds per camera with occlusion on, one cull and the reflections' build (or none) with it off; phase two's draws follow phase one's in the prepass.
- [ ] GL readback: a copy wholly behind a wall is not drawn in phase two once the wall has drawn in phase one; a copy that comes out from behind it is drawn the same frame.
- [ ] GL pixel probes: frames with occlusion on and off are identical for a scene of walls and copies, the camera moving between frames; a pyramid level holds the farthest depth of a known step.

Hand check: the neon city and Sponza are faster with it on; no copy pops in a frame late.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
