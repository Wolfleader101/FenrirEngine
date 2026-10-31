# 06: Physically based bloom in `PostProcess`

**What to build:** Bright pixels bleed into their surroundings before the tonemap, as CoD: Advanced Warfare's bloom does (spec: "The frame"; ADR-0049).

- **A downsample chain** with Jimenez's 13-tap filter (Karis average on the first step against fireflies) and **a tent-filter upsample chain**, as render graph passes per view in `PostProcess`.
- **No threshold:** the result is mixed into `SceneColor` by `Camera`'s `bloom.intensity` (0.04 by default; 0 culls the passes) and `bloom.radius`.
- The API description gains the settings (generated output only).

**Blocked by:** 01

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`

**Status:** resolved

- [x] `RendererProbe` on null: the bloom passes run per view, and are culled at intensity 0.
- [x] GL pixel probes: a single bright pixel spreads symmetrically and energy is preserved within tolerance. (A 2 x 2 spot, the smallest every level halves evenly: see Comments.)
- [x] Golden image: an emissive sphere with bloom.

Hand check: emissive surfaces glow softly with no hard threshold edge.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-31. Decisions to review:

- **`Core/Bloom`** (`BloomChain`), beside `SkyLighting`: `SceneRenderer` collects each drawn view's SceneColor, size and `BloomSettings` (the core's copy of `Camera::bloom`, on `ViewRequest`) and hands them over once, after the views' own passes. All passes are in `PostProcess`: one `BloomDownsample` per level, one `BloomUpsample` per level but the smallest, and a `BloomComposite` into SceneColor.
- **Levels:** one per halving of the viewport's shorter side, at most six (CoD: AW's count), each a transient RGBA16F texture at half the one before. A 1080p view's smallest is 30 x 16; a viewport under 2 pixels on a side blooms nothing. R11G11B10F would halve the bandwidth but the device has no such format yet.
- **`radius` is a scatter, not a filter width.** Each upsample alpha-blends the tent-filtered level below over its own level by `radius`, so level i weighs (1 - r) r^i and the widest gets the rest (`BloomLevelWeights`, the CPU mirror). The weights sum to 1, which the ticket's energy preservation needs: LearnOpenGL's additive chain multiplies the energy by the level count, and its `filterRadius` (tent offset in UV) depends on the resolution. 0 keeps the narrowest blur, 1 the widest; 0.7 by default (Unity's scatter default).
- **Mixed into SceneColor, not in `Output`:** `BloomComposite` alpha-blends the tent of level 0 over SceneColor by `intensity` (clamped to [0, 1]), so Output and every later effect read one bloomed SceneColor. Under MSAA the composite draws at SceneColor's samples (a full-screen triangle writes each sample the same value, so Output's resolve gives the same mix), and the first downsample's `MSAA` variant fetches and averages the samples of the 6 x 6 texels under its taps.
- **Edges are mirrored** (`MirroredRepeat`, and by hand in the MSAA fetches), not clamped. With clamping, the GL energy test lost 4.6% at radius 0.7 once the blur reached the small levels' edges; mirrored, each level is a tile of an endless mirrored image whose mean the filters keep exactly (odd-sided levels are a little off).
- **Karis average** on the first downsample's five boxes (1 / (1 + luma), Rec. 709 luma), as Jimenez's partial Karis. It takes energy from bright spots by design, so the energy tests use a dim spot (0.05): measured losses are 0.2-0.5%.
- **No threshold**, per the spec: everything blooms by `intensity`. The GL test fixtures that read exact colours (`GlScene::AddCamera`, GlSkinScene) turn bloom off, as they set `Tonemapper::None` where they read Output. Null pass-list tests about other things turn it off too; the stats test counts the 12 bloom passes. `RendererProbe::SceneColour` with no size now picks the largest RGBA16F transient, since the bloom levels are RGBA16F too.
- **Tests:** CPU (`Tests/Bloom.cpp`): level counts, level weights. Null: the 6 + 5 + 1 passes in order with their sources, targets, viewports, load ops, blend modes and blocks (texel size, Karis flag, radius, intensity); per view with two cameras; culled at intensity 0; clamping; the MSAA variant and 4-sample composite. GL (`Tests/OpenGL/Bloom.cpp`): a 2 x 2 spot at the window's centre (the smallest spot every level halves evenly; a lone pixel sits off-centre in its 2 x 2 box) blooms mirror-symmetrically across both axes and the diagonal; the frame's energy stays within 1% at intensities 1 and 0.5 and radii 0.2 and 0.7, and within 3% under 4x MSAA read through the 8-bit target; golden `bloom-emissive-sphere.png` (a sphere at emissive strength 24 beside a grey one, the default bloom and AgX).
- **API description and C# binding regenerated** (`fenrir-describe-api`, `fenrir-generate-bindings`): `Fenrir::Bloom` and `Camera.Bloom`. No other golden changed (the fixtures' cameras have bloom off).

- **msvc-x64-debug:** the whole CTest tree passed (166 tests, GL and demo smoke runs included; RTX 5080), then the renderer, scripting and RendererDemo tests (29) again after the review fixes.
- **Review** (`/code-review`, standards and spec): no wrong maths found (taps, Karis, the MSAA indexing and mirroring, tent, level weights, pass order and lifetimes checked). Fixed from it: ADR-0049 amended (`bloom.intensity` / `bloom.radius`, what the radius means, mirrored edges), `INFO` before every check, the two-camera test split in two, `BloomView`'s fields documented, the GL spot setup shared. Left: a fourth copy of the built-in `ReadProgram` helper (worth one shared helper beside `GetBuiltinShader`); the Karis average's loss on bright spots is unbounded by any test (by design: it is the firefly suppression); on odd-sided sources the MSAA path's `round` corner and the bilinear path can differ by a texel.

Hand check still to do: emissive surfaces glowing softly with no threshold edge, in LightingDemo (ticket 07).
