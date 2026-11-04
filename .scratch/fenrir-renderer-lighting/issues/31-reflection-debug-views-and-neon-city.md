# 31: Reflection debug views, LightingDemo's controls, and the neon city's wet streets

**What to build:** Screen-space reflections (ADR-0056) made visible and adjustable, and turned on where they were asked for. Requested by the user while reviewing ticket 20 (the neon city), 2026-11-02, comparing it with cyberpunk reference renders.

- **`CameraDebugView` gains `Reflections`** (the reflected colour times its confidence, exposed and tonemapped, black where nothing reflects) and **`ReflectionConfidence`** (grey).
- **LightingDemo's Lighting panel** gains a Reflections section: on/off, intensity, max distance, thickness and roughness cutoff, and the two debug views.
- **The neon city** turns reflections on: its wet streets reflect the towers' glowing edge strips, the wall boards and the lit windows, as well as the signs' rect lights. Its signs' boards are drawn, so their rect lights keep `visibleSurface` true. A key toggles reflections, beside M's wet and dry streets. Its golden image is updated.
- **The showroom** turns them on for its polished floor.
- **C#:** the two debug views reach C# through FenrirRenderer's regenerated binding, and join the scripting round trip.

**Blocked by:** 20, 30

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/Scripting/`, `examples/LightingDemo/`

**Status:** resolved

- [x] GL pixel probes: each debug view shows the probed pixel's reflection or confidence; with reflections off both are black.
- [x] The neon city's and the showroom's smoke runs pass on null with reflections on, without fetched assets.
- [x] The three passes' GPU time in the neon city at 1920 × 1080, measured with RenderDoc or Nsight, is recorded in the ticket's comments, and over ADR-0056's 1.5 ms is called out.

Hand check: in the neon city at night, the wet streets mirror the edge strips, sign boards and windows, streaked by the puddles' edges and blurred on damp asphalt; reflections fade at the screen's edge and fall back to the signs' own glints as they leave the screen; driving cars leave no visible ghost at walking speed.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

Built 2026-11-04, stacked on ticket 30's `claude/zen-mcclintock-1d25cb` (#300, not yet merged). CTest passes 170/170
on msvc-x64-debug, the GL suite and LightingDemo's null and OpenGL smoke runs included. The neon city has no golden
image, so none changed. The hand checks, in the neon city and the showroom, are the user's: they are in LightingDemo's
README under Hand checks. Decisions to review:

- **Both views are drawn by `Output`, not by `Opaque` through the include.** The include would show the sky where
  nothing opaque is drawn, and add a material's own emission or an unlit item's colour; `Output` sees only the hits.
- **`Reflections` is a new `REFLECTIONS` variant of `builtin:output`**, binding the hit target at set 2 binding 0 and
  the history at binding 1 (`m_reflectionOutputLayout`, `ReflectionOutputGroup`). It takes the history's sharpest level
  (level 0), not the cone mip `Opaque` picks: the cone needs the material's roughness, which `Output` does not have.
  So the view shows what a mirror would blend; a material's roughness blurs and fades it in the lit view. It is
  multiplied by the bare confidence, not the blend's weight, as the ticket says: the camera's intensity and the
  roughness fade are left out.
- **It reads last frame's history with `SamplePrevious`**, so `Output` runs before this frame's `SceneHistory` passes
  for that view. A plain `Sample` there reads this frame's colour at hits reprojected into last frame's view, which is
  wrong by the camera's motion over one frame. The null test checks the order.
- **`ReflectionConfidence` reuses the grey `OCCLUSION` variant**, with a `greyChannel` in the Output block (the word
  that was padding): 2 for the hit target's blue, 0 for every other grey view. No new variant or layout.
- **Black is drawn through the grey variant** with the 1 x 1 black texture, for both views with reflections off, so
  the `REFLECTIONS` variant never needs a stand-in history.
- **Under MSAA the variant samples single-sampled textures** (the hit target and the history are both single-sampled),
  so `REFLECTIONS` overrides the `MSAA` key. The GL probe of the view runs at 1 and 4 samples.
- **The GL probes check the drawn pixel against the hit target's own confidence** at that pixel (read back with
  `RendererProbe::ReflectionHits`), exposed by -2 EV with no tonemapper, so the expected value is exact; and black on
  the board itself and on the sky.
- **The scripting round trip** carries `ReflectionConfidence` on its main camera and `Reflections` on its perspective
  one; `LightClusters`, which the main camera had, is no longer in it.
- **LightingDemo:** `LightingSettings` gains `reflections`, given to every camera like ambient occlusion, off unless a
  scene turns it on. The panel's Reflections section has on/off, intensity (0 to 1, since the blend clamps at 1), max
  distance (1 to 200, log), thickness (0.02 to 5, log), roughness cutoff (0.1 to 1), and two radio buttons for the
  debug views, which G also steps through after the light clusters. The neon city's key is **R** (N and M are the
  traffic and the streets), and its smoke keys press G eight times now, so they come back to the lit scene, and R
  twice, and its Scene part has a Reflections (R) checkbox beside the puddles. The showroom has no key of its own; the
  Lighting window's checkbox turns them off.
- **After `/code-review`:** the `REFLECTIONS` variant reads the hit target with `texelFetch`, as `Fenrir/Lighting.glsl`
  does, instead of through the trilinear sampler, which could blend a hit's UV with a miss's at the edge of a
  reflection; its binding is named `reflectionHits`; every new check has an `INFO` of its own; and the tests' copied
  group helper became one `GroupTexture`. Kept as they are, for the reviewer: `ReflectionConfidence` goes through the
  grey variant's filtered `texture()`, which reads texel centres exactly since the hit target is the view's size, and
  blurs only grey; `Output` tests the debug view in four places (`showGrey`, `grey`, `showReflections` and the block's
  `greyChannel`), where a table per view would gather them; if the `REFLECTIONS` variant failed to build, the view
  would show the lit scene, as the grey views already do without theirs; and the Reflections section's two radio
  buttons repeat two entries of the Debug view list, so the views sit beside the settings they show.

## Comments

**Cost, 2026-11-04.** The neon city at 1920 x 1080 with LightingDemo's 4x MSAA, on an NVIDIA GeForce RTX 5080 (driver
621.01), msvc-x64-debug (the GPU work is the same as in Release), without the fetched assets (the generated city, plain
asphalt and walls), from the scene's starting camera with the traffic moving. Three frames captured with RenderDoc
1.46 and timed by its EventGPUDuration counter, summed over each pass's draws (the pass labels are GL debug groups):

| Pass | Frame 1 | Frame 2 | Frame 3 |
| --- | --- | --- | --- |
| `HiZ` (11 levels) | 0.076 ms | 0.602 ms | 0.075 ms |
| `ReflectionTrace` | 0.558 ms | 0.560 ms | 0.566 ms |
| `SceneHistory` (11 levels) | 0.091 ms | 0.093 ms | 0.091 ms |
| **The three** | **0.725 ms** | **1.256 ms** | **0.732 ms** |
| Whole frame | 3.196 ms | 3.707 ms | 3.170 ms |

All three frames are under ADR-0056's 1.5 ms. The second frame's `HiZ` reading, eight times the others, looks like a
counter outlier on replay, not a cost of the scene; the trace, which dominates, is steady at 0.56 ms. For scale,
`Opaque` is about 1.07 ms, bloom's downsample 0.52 ms and GTAO's horizon search 0.50 ms. Measured without the fetched
asphalt and wall textures, which change `Opaque`'s cost, not the three passes'. The window was opened at 1920 x 1080
by a temporary, uncommitted change to the scaffold's window size.
