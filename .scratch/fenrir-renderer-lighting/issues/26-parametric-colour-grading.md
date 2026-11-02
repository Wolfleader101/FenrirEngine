# 26: Parametric colour grading

**What to build:** A per-camera colour grade in the Output pass, scene-referred and before the tonemap (ADR-0057), mirrored on the CPU as the tonemappers are. Requested by the user while tuning ticket 20 against cyberpunk reference renders, 2026-11-02: the neon city's look (teal shadows, magenta highlights, punchy contrast) cannot be reached by exposure and the tonemapper alone.

- **`Camera` gains `colourGrading`**, identity by default and skipped when it is identity:
  - **White balance:** `temperature` and `tint`, a von Kries adaptation in LMS space (CAT02 or Bradford, named in the code) from the white they describe to D65, as Unity's and Unreal's white balance are.
  - **Saturation** around Rec. 709 luminance.
  - **Contrast** in log2 around mid grey (0.18), so it steepens the curve without moving mid grey or clipping, and the tonemapper still rolls the highlights off.
  - **ASC CDL** per channel: `slope`, `offset` and `power`, applied in linear with negatives clamped before the power. CDL is a published standard with an exact formula, which the tests can hold it to; lift, gamma and gain are display-referred and wait for a display-referred stage (ADR-0057).
- The grade runs after exposure and the vignette, before the tonemapper, in `builtin:output` and in a CPU mirror beside `src/Core/Tonemapping.cpp`; a change to one is a change to both, and the GL tests hold the shader to the mirror.
- The C# binding is regenerated from the header (`packages/FenrirRenderer/Scripting/api/`, `Managed/Fenrir.Renderer/Generated/`).
- LightingDemo's panel gains temperature, tint, saturation and contrast; the neon city sets a grade that suits it.
- **Not here:** 3D LUTs (ADR-0057's "Not yet"): they need 3D textures, which `TextureKind` does not have, and a way to author them.

**Blocked by:** 20

**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `packages/FenrirRenderer/Managed/`, `examples/LightingDemo/`

**Status:** needs-triage

- [ ] CPU unit tests: the identity grade returns its input exactly; 6500 K and no tint leave D65 white white; contrast keeps 0.18 at 0.18; saturation 0 gives grey of the input's luminance; CDL matches its formula for known values.
- [ ] GL pixel probes: flat-colour views graded by each control match the CPU mirror, through the tonemapper, within the probes' tolerance.
- [ ] A golden image of the tonemap test scene under a non-trivial grade.

Hand check: in the neon city, warming and cooling the white balance and raising contrast change the mood without clipping the signs or crushing the streets.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
