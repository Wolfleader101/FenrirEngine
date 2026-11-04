# 29: Scene history colour and the Hi-Z pyramid

**What to build:** The two per-view textures screen-space reflections read (ADR-0056), kept across frames. Requested by the user while reviewing ticket 20 (the neon city), 2026-11-02: wet streets mirror only the analytic lights and the sky, not the neon strips, sign boards and windows. This ticket makes no reflection yet; ticket 30 traces and blends.

- **Per-camera persistent textures**, imported into each frame's render graph with `ImportTexture` as the shadow atlases are: the history colour (RGBA16F, the view's size, a full mip chain) and two Hi-Z pyramids (R32F, the view's size, a full mip chain) that swap each frame, so the trace can read last frame's as well as this frame's. Freed when the camera goes; remade, and the history marked invalid, when the view's size or MSAA changes. A camera with reflections off has none and adds no pass.
- **`HiZ` passes** in the `DepthPrepass` stage, after GTAO's: level 0 copied from `SceneDepth` (sample 0 under MSAA, as `builtin:gtao` reads it), then each level drawn into the next mip by a fragment pass taking the largest (nearest, reversed-Z) depth of the texels it covers, the odd row and column folded in when a level's size is odd. The device has no storage images, so these are fragment passes into mips, as the bloom chain's are.
- **`SceneHistory` passes** at the start of `PostProcess`, before bloom: `SceneColor` copied into the history's mip 0 (a plain average of the samples under MSAA, not bloom's Karis average), then box-filtered down its mips.
- **The previous frame's view-projection** kept per camera, with a history-valid flag the trace will read: false on the camera's first frame and after a remake.
- **`Camera::reflections`**' `enabled` switch lands here, as the switch for these passes; ticket 30 adds the rest of the settings. FenrirRenderer's C# binding is regenerated for it, and `Scripting/Tests/Lighting.cpp`'s camera round trip covers it.

**Blocked by:** 06, 11

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/README.md`

**Status:** resolved

- [x] CPU unit tests: the pyramid's level sizes for even and odd views; the reduction keeps the nearest depth when a level's size is odd.
- [x] Null `RendererProbe`: a camera with reflections on records the HiZ passes after GTAO's and the SceneHistory passes before the first BloomDownsample; one with them off records neither; two cameras keep two histories.
- [x] GL pixel probes: the history's mip 0 holds last frame's pre-bloom SceneColor at a probed pixel, MSAA included; a pyramid level holds the nearest depth of a known step.
- [x] Resizing a view marks its history invalid for one frame.

Hand check: none; ticket 30 shows the result.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

Built 2026-11-04, stacked on `claude/ssr-spec` (#282, not yet merged). Decisions to review:

- **A binding can name one mip, a small device change ADR-0056 said was not needed.** Drawing level n of a texture
  while sampling level n - 1 of the same texture is a feedback loop in OpenGL unless the sampled levels exclude the one
  drawn (GL 4.6, 9.3.1), however the shader reads. So `BindingGroupEntry` gains `mipLevel`: a binding of one level,
  which the GL backend makes the texture's base and maximum level when it binds it, and widens again when the texture
  is next bound whole (the explicit backends would use a one-level view). `FullscreenPasses` gains `AddMipPass`, which
  samples one level and draws into another, and its source groups are cached by mip too. Bloom's separate textures per
  level were the other way, but the ticket and ticket 30's trace want one texture with mips, sampled across levels.
  A pass drawing into a texture widens its levels again first, so a level drawn is never outside the texture's range.
  ADR-0056's "need no new device work" now says this.
- **R32F is a new `TextureFormat` (`R32Float`).** The pyramid holds depth to full float precision; RG16F or R16F would
  lose the near-plane precision reversed-Z keeps. `Draw2DFeature`'s format names for the log gain RG16F and R32F; the
  list had stopped one short of the formats since RG16F was added.
- **Cameras are told apart by entity id.** `ViewRequest` gains `camera`, the entity id, which the textures are kept
  under. A camera whose entity is destroyed and whose id is reused by a new camera in the same frame would inherit the
  textures, and the history would be valid for a frame it should not be; ticket 30's disocclusion and reprojection keep
  that to one frame. A camera with two views in one frame (only possible when the core is driven directly) gets
  textures for its first only.
- **Freed after one frame without the camera.** A camera disabled, deleted, made unable to draw or with reflections
  turned off has its textures freed at the end of that frame, so turning reflections back on starts invalid. A frame the
  window is minimised draws nothing and frees nothing.
- **A view that cannot draw this frame loses its textures.** A view into a render target image the renderer has not
  prepared yet is skipped, so its camera has no view that frame and starts invalid the next.
- **Reflections keep the scene's passes from being culled.** The HiZ and SceneHistory passes write textures kept
  outside the graph, which the graph never culls, so a view showing a grey debug view (its occlusion or a shadow atlas)
  still draws Opaque and the rest when its camera has reflections on.
- **"Remade when MSAA changes" needs no check.** The scene renderer's sample count is fixed at install, so only the
  view's size remakes the textures; they are always single-sampled.
- **Full mip chains to 1 x 1**: 11 levels at 1920 x 1080, so 11 HiZ and 11 SceneHistory passes a view. The history's
  mip count is in `HistoryView::mips`, for `ViewData::reflectionInfo` in ticket 30.
- **The history's box filter is 2 x 2, clamped.** Where a level's side is odd, the last row or column of the level
  above is left out of the history's mips (not of the pyramid's, where leaving it out would hide geometry). The
  reflection blur only needs a cone-width average, so a missing texel row at the edge of the blur costs nothing visible.
- **The history copy drops NaN and clamps infinities either way**, as bloom's first level does, so one bad pixel does not spread
  through every level and into every reflection of it.
- **What the previous frame is.** `HistoryView::previousViewProjection` is the view-projection of the frame that drew
  the history and the previous pyramid; on a camera's first frame, and after a remake, it is this frame's and `valid`
  is false. `previousPyramid` is imported each frame for ticket 30 to read; this ticket's passes do not read it.
- **The probes use committed assets only**: unlit quads under an orthographic camera one unit a pixel. A 48 x 48
  viewport, whose pyramid runs 6, 3, 1, probes the odd-size fold on the GPU as `ReduceHiZ` does on the CPU. The MSAA probe
  checks that a pixel an edge halves holds between a fifth and four fifths of the colour, not an exact half, since GL
  leaves the 4x sample positions to the driver.
