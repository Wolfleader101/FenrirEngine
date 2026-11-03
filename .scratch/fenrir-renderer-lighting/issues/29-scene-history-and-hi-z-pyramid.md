# 29: Scene history colour and the Hi-Z pyramid

**What to build:** The two per-view textures screen-space reflections read (ADR-0056), kept across frames. Requested by the user while reviewing ticket 20 (the neon city), 2026-11-02: wet streets mirror only the analytic lights and the sky, not the neon strips, sign boards and windows. This ticket makes no reflection yet; ticket 30 traces and blends.

- **Per-camera persistent textures**, imported into each frame's render graph with `ImportTexture` as the shadow atlases are: the history colour (RGBA16F, the view's size, a full mip chain) and two Hi-Z pyramids (R32F, the view's size, a full mip chain) that swap each frame, so the trace can read last frame's as well as this frame's. Freed when the camera goes; remade, and the history marked invalid, when the view's size or MSAA changes. A camera with reflections off has none and adds no pass.
- **`HiZ` passes** in the `DepthPrepass` stage, after GTAO's: level 0 copied from `SceneDepth` (sample 0 under MSAA, as `builtin:gtao` reads it), then each level drawn into the next mip by a fragment pass taking the largest (nearest, reversed-Z) depth of the texels it covers, the odd row and column folded in when a level's size is odd. The device has no storage images, so these are fragment passes into mips, as the bloom chain's are.
- **`SceneHistory` passes** at the start of `PostProcess`, before bloom: `SceneColor` copied into the history's mip 0 (a plain average of the samples under MSAA, not bloom's Karis average), then box-filtered down its mips.
- **The previous frame's view-projection** kept per camera, with a history-valid flag the trace will read: false on the camera's first frame and after a remake.
- **`Camera::reflections`**' `enabled` switch lands here, as the switch for these passes; ticket 30 adds the rest of the settings. FenrirRenderer's C# binding is regenerated for it, and `Scripting/Tests/Lighting.cpp`'s camera round trip covers it.

**Blocked by:** 06, 11

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/README.md`

**Status:** ready-for-agent

- [ ] CPU unit tests: the pyramid's level sizes for even and odd views; the reduction keeps the nearest depth when a level's size is odd.
- [ ] Null `RendererProbe`: a camera with reflections on records the HiZ passes after GTAO's and the SceneHistory passes before the first BloomDownsample; one with them off records neither; two cameras keep two histories.
- [ ] GL pixel probes: the history's mip 0 holds last frame's pre-bloom SceneColor at a probed pixel, MSAA included; a pyramid level holds the nearest depth of a known step.
- [ ] Resizing a view marks its history invalid for one frame.

Hand check: none; ticket 30 shows the result.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
