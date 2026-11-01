# 10: Copies hidden behind others are culled against a depth pyramid, in two phases

**What to build:** Occlusion culling for cameras (spec: "Occlusion"; ADR-0055 "Not yet", which this ticket's ADR amends). Start it only when a scene, measured after ticket 07, shows hidden copies costing a frame; otherwise it stays ready and unbuilt.

- **Persistent visibility:** one bit per candidate per camera, kept between frames while the frame's candidates keep their order (reset when they change).
- **Phase one:** the cull pass, as now, but only candidates visible last frame are drawn into `DepthPrepass`.
- **The pyramid:** a compute pass reduces the prepass depth into a mip chain of the farthest depth per texel (minimum under reversed-Z), at the view's resolution rounded down to a power of two, resolving MSAA by the farthest sample.
- **Phase two:** every candidate not drawn in phase one and inside the frustum is tested by its sphere's screen rectangle against the pyramid level whose texels cover it in at most 2x2; those that show are drawn into the prepass after phase one's. Visibility bits are rewritten from both phases. `Opaque` draws both phases' survivors.
- **`RendererConfig::occlusionCulling`** (off until measured otherwise), the API description gaining it (generated output only); `RendererStats` counts each phase's draws.
- **Shadow views do not occlusion cull.** An ADR records the decision and the measured scene.

**Blocked by:** 07

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/api/`, `docs/adr/`

**Status:** ready-for-agent

- [ ] CPU unit tests: a sphere's screen rectangle and pyramid level under a perspective view; the test keeps a sphere that straddles the near plane.
- [ ] `RendererProbe` on null: two cull dispatches and a pyramid pass per camera with occlusion on, one cull and no pyramid with it off; phase two's draws follow phase one's in the prepass.
- [ ] GL readback: a copy wholly behind a wall is not drawn in phase two once the wall has drawn in phase one; a copy that comes out from behind it is drawn the same frame.
- [ ] GL pixel probes: frames with occlusion on and off are identical for a scene of walls and copies, the camera moving between frames.

Hand check: a scene with heavy occlusion (the neon city or Sponza with instanced props) is faster with it on; no copy pops in a frame late.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
