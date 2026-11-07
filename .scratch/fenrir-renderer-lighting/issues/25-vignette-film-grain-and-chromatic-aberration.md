# 25: Vignette, film grain and chromatic aberration

**What to build:** Three cheap camera effects in the Output pass, in ADR-0059's order. Requested by the user while tuning ticket 20 (the neon city) against cyberpunk reference renders, 2026-11-02; out of scope for ticket 20.

- **Chromatic aberration:** each channel of SceneColor read at its own radial offset from the view's centre, red outwards and blue inwards, by `intensity` as a fraction of the view's half-diagonal at its corners (0 off, and off costs nothing). Under MSAA each offset texel is resolved as the plain read is, so it is three resolves instead of one; a shader key on `builtin:output`, as `MSAA` and `OCCLUSION` are, keeps the plain path as it is.
- **Vignette:** the linear colour multiplied by a falloff towards the corners, before the grade and the tonemap (ADR-0059), so it darkens as a lens does and the tonemapper still sees it: `intensity` (0 off), `radius` where it starts, `smoothness`, and `roundness` (1 a circle whatever the view's aspect, 0 the view's own rectangle).
- **Film grain:** noise added after the tonemap, display-referred, scaled by `intensity` (0 off) and weighted towards the mid-tones by `response`, as film grain shows least in deep shadow and bright highlight. The noise is a hash of the pixel and the frame index, so it moves each frame and a test's frame is reproducible; `size` scales its cells.
- `Camera` gains `chromaticAberration { intensity }`, `vignette { intensity, radius, smoothness, roundness }` and `filmGrain { intensity, response, size }`, all off by default, and they reach `SceneFrame` and `builtin:output`'s `OutputData` block. They are skipped under a `CameraDebugView` that shows an input as grey.
- The C# binding is regenerated from the header (`packages/FenrirRenderer/Scripting/api/`, `Managed/Fenrir.Renderer/Generated/`).
- LightingDemo's panel gains each effect's main setting; the neon city turns on a light vignette, fine grain and a little chromatic aberration.

**Blocked by:** 27

**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `packages/FenrirRenderer/Managed/`, `examples/LightingDemo/`

**Status:** resolved

- [x] CPU unit tests: the vignette's falloff is 1 at the centre and matches its closed form at the corners for both roundness extremes; the grain's hash is the same for the same pixel and frame and differs between frames.
- [x] GL pixel probes: with chromatic aberration, a white bar near the view's edge shows red outside and blue inside, and the centre pixel is unchanged; with the vignette, a flat grey view is darker at the corners by the CPU falloff; with each effect at 0, the frame matches the tonemap goldens exactly.
- [x] Golden images: each effect on its own over the tonemap test scene, grain at a fixed frame index.

Done as built (2026-11-06): roundness 0 is an ellipse in the view's own proportions (the falloff measured in each axis' own half-length), not a rectangle; the grain is added in a 2.2 gamma encoding of the tonemapped colour, its noise a PCG3D hash summed into a triangle on [-1, 1); the `Reflections` debug view, exposed and graded as the lit view is, takes the vignette and grain too but never the aberration. The coloured debug views drawn through the lit path (`ShadowCascades`, `LightClusters`) take all three, as they take the exposure and the grade. The GL probes also hold the grain to its CPU mirror pixel by pixel, and check that a grey debug view is unchanged by every effect.

Hand check: in the neon city, the corners fall off gently, signs near the edge fringe red and blue, and grain shimmers over the dark streets without showing in the bright signs.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
