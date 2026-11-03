# 01: `Output` tonemaps with exposure, and golden-image tests arrive

**What to build:** `Output` replaces its clamp with exposure and a tonemapper chosen per camera, and the renderer gains whole-frame golden-image tests for this family to build on (spec: "The frame", "Testing"; ADR-0049).

- **`Camera` gains `exposure`** in EV stops (0 by default; the colour is scaled by `2^exposure`) and **`tonemapper`**: `None` (the old clamp), `AgX` (default), `PbrNeutral` (Khronos PBR Neutral) and `Aces` (fitted). Each is a variant or branch of `output.frag`, still resolving MSAA and still sRGB-encoded by the target.
- **Golden-image tests** under `packages/FenrirRenderer/Tests/OpenGL/`: render a fixed scene to an image target, compare with a committed PNG under a perceptual tolerance (FLIP, or per-pixel ΔE with a maximum count of differing pixels), write the actual and difference images on failure, and regenerate goldens only with an explicit flag or script. Skipped where no GL device exists, like the other GL tests.
- Existing scenes now default to `AgX`; RendererDemo's and AnimationDemo's look is checked by hand and their READMEs say so.
- `exposure` and `tonemapper` reach FenrirRenderer's API description (generated output only; C# is ticket 22).

**Blocked by:** none

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `examples/RendererDemo/`

**Status:** resolved

- [x] CPU unit tests: each tonemapper's curve at 0, mid grey, 1 and a bright HDR value against reference values; EV scaling.
- [x] Golden images: one lit scene per tonemapper and at three exposures; the comparison fails on a deliberately changed image.
- [x] `RendererProbe` on null: each camera's settings reach its view's `Output` pass.

Hand check: RendererDemo's scenes look right under `AgX`, and switching tonemappers on a bright scene shows the differences.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-31. Decisions to review:

- **The block.** `Output` binds its own uniform block at set 1 (`OutputData { exposureScale, tonemapper }`, a ring like the background's), not new fields in `Fenrir/View.glsl`, so user shaders' view set is unchanged. The tonemapper is a branch on that uniform, not four shader variants. If a later ticket wants exposure in materials (pre-exposed emissive), it moves to the view block then.
- **The curves.** AgX is three.js r170's (Filament's and Blender's fit, with its Rec. 2020 round trip and 2.2 linearisation), PBR Neutral is Khronos's reference, and ACES is Stephen Hill's fit, without three.js's 1/0.6 pre-scale. The renderer core mirrors them in C++ (`src/Core/Tonemapping.cpp`), as Skinning does: the CPU tests hold the mirror to reference values worked from the published formulas in double precision (independently of the C++; they show the port is faithful, not that the formulas are), and a GL test holds `output.frag` to the mirror with flat colours through every tonemapper at three exposures.
- **`Render::Tonemapper`.** The core cannot include `FenrirRenderer/Components.hpp`, so it has its own enum, cast across with static_asserts as `Vsync` is.
- **Golden images.** CIEDE2000 per pixel, a pixel differing above 2, at most 16 of 96 x 64 differing; `FENRIR_UPDATE_GOLDENS=1` writes them; failures go to `golden-failures/` in the tests' build directory. The comparison lives in `FenrirRendererGoldenImages` (a static library of the tests, linking FenrirAssets's `FenrirStbImage` and compiling `stb_image_write` itself) so the null tests check it and the GL tests use it. Goldens made on an RTX 5080 (driver 617.14); other GPUs untested. A changed image failing is tested on CPU images, not a GL frame; no golden runs with MSAA, so tonemapping after the resolve is read from the shader, not tested.
- **Existing GL tests.** Three in `Targets.cpp` read image targets exactly, so their cameras set `Tonemapper::None`; tests reading `SceneColor` were unaffected.
- **C#.** Adding fields to `Camera` changes its layout, and CI checks the generated bindings, so `fenrir-describe-api` and `fenrir-generate-bindings` were run: `Camera.cs` gains `Exposure` and `Tonemapper`, and `Tonemapper.cs` is new. No hand-written C# or C# tests; ticket 22 adds the round-trip tests.
- **RendererDemo.** T and E in the Sky scene step the free-fly camera's tonemapper and exposure, not in Meshes and materials, whose keys its C# twin shares and could not honour before ticket 22. Under AgX the LDR skyboxes read paler and the unlit colours lighter than before; both READMEs say so.
- **Glossary.** Adds *Exposure*, *Tonemapper* and *Golden image*.
