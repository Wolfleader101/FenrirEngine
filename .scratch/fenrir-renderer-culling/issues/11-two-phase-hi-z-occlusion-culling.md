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

**Status:** resolved

- [x] CPU unit tests: a box's screen rectangle and pyramid level under a perspective view; the test keeps a box that straddles the near plane; the pyramid's level sizes for even and odd views, and the reduction keeps the farthest depth when a level's size is odd.
- [x] `RendererProbe` on null: two cull dispatches and two pyramid builds per camera with occlusion on, one cull and the reflections' build (or none) with it off; phase two's draws follow phase one's in the prepass.
- [x] GL readback: a copy wholly behind a wall is not drawn in phase two once the wall has drawn in phase one; a copy that comes out from behind it is drawn the same frame.
- [x] GL pixel probes: frames with occlusion on and off are identical for a scene of walls and copies, the camera moving between frames; a pyramid level holds the farthest depth of a known step.

Hand check: the neon city and Sponza are faster with it on; no copy pops in a frame late.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

- Phase two tests every candidate inside the frustum, those phase one drew included, and writes every word from its result: with only the undrawn tested, a copy drawn in phase one would keep its word for ever and never be culled once something hid it. The cost is one pyramid read per candidate; ADR-0057 says so.
- The pyramid is built once for a camera without reflections (after phase one's draws) and twice for a reflecting one (again from the complete depth, before the trace): nothing but the trace reads the complete build, and the graph would otherwise run eleven passes nobody reads. The `RendererProbe` test checks two builds with reflections on and one without, not two for every camera.
- Visibility is a word per candidate, not a bit: the one buffer is read by phase one and read then written by phase two, so a bit would need two buffers or atomics for the same information. It is kept while the view's runs keep their draw-list index, mesh, list and count, in order, and treated as stale (every candidate visible, so phase one draws everything) for the frame they change in; a frame with one phase (occlusion off, GPU culling off, or frozen) forgets it.
- The render graph gained `PassBuilder::StorageReadPrevious`, the buffer twin of `SamplePrevious`: phase one reads last frame's words before phase two writes them, which a plain read would order after the write and close a cycle. The device gained `TextureFormat::Rg32Float`.
- The frame's template, level table and group table rings are now reserved before the views, for the most the frame's draws could take, so each view's passes bind them in registration order (the second cull between the first prepass and the second); they are written after the views as before.
- `examples/LightingDemo/` is touched beyond the Touches line, as ticket 08's note asked: the asteroid ring's panel and keys gained the occlusion switch (V) and the `drawnByPhase` line.
- The `/code-review` findings are fixed in the branch's later commits.
