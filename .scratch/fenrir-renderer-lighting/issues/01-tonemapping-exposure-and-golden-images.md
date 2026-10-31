# 01: `Output` tonemaps with exposure, and golden-image tests arrive

**What to build:** `Output` replaces its clamp with exposure and a tonemapper chosen per camera, and the renderer gains whole-frame golden-image tests for this family to build on (spec: "The frame", "Testing"; ADR-0049).

- **`Camera` gains `exposure`** in EV stops (0 by default; the colour is scaled by `2^exposure`) and **`tonemapper`**: `None` (the old clamp), `AgX` (default), `PbrNeutral` (Khronos PBR Neutral) and `Aces` (fitted). Each is a variant or branch of `output.frag`, still resolving MSAA and still sRGB-encoded by the target.
- **Golden-image tests** under `packages/FenrirRenderer/Tests/OpenGL/`: render a fixed scene to an image target, compare with a committed PNG under a perceptual tolerance (FLIP, or per-pixel ΔE with a maximum count of differing pixels), write the actual and difference images on failure, and regenerate goldens only with an explicit flag or script. Skipped where no GL device exists, like the other GL tests.
- Existing scenes now default to `AgX`; RendererDemo's and AnimationDemo's look is checked by hand and their READMEs say so.
- `exposure` and `tonemapper` reach FenrirRenderer's API description (generated output only; C# is ticket 22).

**Blocked by:** none

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `examples/RendererDemo/`

**Status:** ready-for-agent

- [ ] CPU unit tests: each tonemapper's curve at 0, mid grey, 1 and a bright HDR value against reference values; EV scaling.
- [ ] Golden images: one lit scene per tonemapper and at three exposures; the comparison fails on a deliberately changed image.
- [ ] `RendererProbe` on null: each camera's settings reach its view's `Output` pass.

Hand check: RendererDemo's scenes look right under `AgX`, and switching tonemappers on a bright scene shows the differences.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
