# 26: Parametric colour grading

**What to build:** A per-camera colour grade in the Output pass, scene-referred and before the tonemap (ADR-0059), mirrored on the CPU as the tonemappers are. Requested by the user while tuning ticket 20 against cyberpunk reference renders, 2026-11-02: the neon city's look (teal shadows, magenta highlights, punchy contrast) cannot be reached by exposure and the tonemapper alone.

- **`Camera` gains `colourGrading`**, identity by default and skipped when it is identity. Its steps, in this order (settled in grilling, 2026-11-03):
  1. **White balance:** `temperature` and `tint`, a von Kries adaptation in LMS space (CAT02 or Bradford, named in the code) from the white they describe to D65, as Unity's and Unreal's white balance are. First, since it corrects the light source.
  2. **Contrast** in log2 around mid grey (0.18), so it steepens the curve without moving mid grey or clipping, and the tonemapper still rolls the highlights off.
  3. **Split toning:** `shadowTint` and `highlightTint`, linear `Colour`s (grey is none), each normalised to unit luminance so it shifts hue and not brightness, its saturation its strength; and `balance` in [-1, 1], which moves the split in log2 around mid grey. The neon city's teal shadows and magenta highlights in one setting.
  4. **Saturation** around Rec. 709 luminance, after split toning, so desaturating fades the tints too.
  5. **ASC CDL** per channel: `slope`, `offset` and `power`, applied in linear with negatives clamped before the power, as the final trim. CDL is a published standard with an exact formula, which the tests can hold it to.
- The grade runs after exposure (and the vignette, once ticket 25 lands), before the tonemapper, in `builtin:output` and in a CPU mirror beside `src/Core/Tonemapping.cpp` that follows the order step for step; a change to one is a change to both, and the GL tests hold the shader to the mirror.
- The C# binding is regenerated from the header (`packages/FenrirRenderer/Scripting/api/`, `Managed/Fenrir.Renderer/Generated/`).
- LightingDemo's panel gains temperature, tint, contrast, the two tints (colour pickers), balance and saturation; the neon city sets a grade that suits it.
- **Not here** (ADR-0059's "Not yet"): 3D LUTs, which need 3D textures (`TextureKind` has none) and a way to author them; lift, gamma and gain, and shadows, midtones and highlights wheels.

**Blocked by:** 20

**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `packages/FenrirRenderer/Managed/`, `examples/LightingDemo/`

**Status:** resolved

- [x] CPU unit tests: the identity grade returns its input exactly; 6500 K and no tint leave D65 white white; contrast keeps 0.18 at 0.18; grey split tints change nothing, and a tinted mid grey keeps its luminance; saturation 0 gives grey of the input's luminance; CDL matches its formula for known values.
- [x] GL pixel probes: flat-colour views graded by each control match the CPU mirror, through the tonemapper, within the probes' tolerance.
- [x] A golden image of the tonemap test scene under a non-trivial grade.

Hand check: in the neon city, warming and cooling the white balance, raising contrast and split-toning teal shadows against magenta highlights change the mood without clipping the signs or crushing the streets.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Done on `claude/lighting-26-colour-grading-231e55`, into `fenrir-camera-post-effects`.

- **msvc-x64-debug:** the whole CTest tree passed (170 tests, GL included; RTX 5080): 14 CPU cases in
  `Tests/ColourGrading.cpp`; in `Tests/OpenGL/Tonemapping.cpp` a probe of 9 grades (each control on its own, the tints
  with a balance, and all together) x 4 flat colours through None at -0.5 EV and AgX at +1 EV against the mirror, the
  golden `tonemap-agx-graded.png` and a check that the grade changes the scene. The C# round trip sets a grade.
- **White balance:** the white is CIE daylight at the temperature (scaled by 1.4388 / 1.438, as CIE 15 defines the D
  illuminants, so 6500 K is D65), held to 2500 to 25000 K; the tint moves it at right angles to the locus in CIE 1960 uv,
  0.02 Duv per unit, positive greener; CAT02 adapts it to the formula's own 6500 K white, which is D65 to about 1e-4 in
  xy, so the default is an exact identity. The tests hold it to the published D50 and D75 chromaticities.
- **Split toning:** the tints cross by smoothstep over four stops either side of the split, and a balance of 1 moves the
  split four stops down from mid grey.
- **Hand check, in part:** the neon city on OpenGL without the fetched assets
  (`screenshots/26-colour-grading/neon-city-graded.png`): teal in the shadowed street, magenta on the lit walls, the
  signs unclipped and the street not crushed. With the fetched assets, the same street with no grade, the neon city's
  grade, 9500 K, 4200 K, and contrast 1.45 with saturation 1.25 (`neon-city-*.jpg` there, set by a temporary,
  uncommitted switch). Sweeping the white balance, contrast and balance by hand is left to the user.
