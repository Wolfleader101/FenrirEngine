# 27: Anamorphic streaks

**What to build:** Horizontal light streaks from bright pixels, as an anamorphic lens draws them, in `PostProcess` after bloom (ADR-0057). Requested by the user while tuning ticket 20 against cyberpunk reference renders, 2026-11-02: the neon signs and headlights want the long blue streaks of the genre.

- **A streak chain beside the bloom chain** (`src/Core/Bloom.cpp`): it starts from bloom's level 0, Karis-averaged already, and halves only the width at each level, the height staying level 0's, with a horizontal-only filter; then upsamples back with a horizontal tent, each level blended over the next by `length` as bloom's levels are by its radius. Variants of `bloom_downsample.frag` and `bloom_upsample.frag` (a key for the axis), or their own small shaders if a variant reads worse.
- **Mixed into SceneColor as bloom is** (ADR-0057): SceneColor becomes (1 - `intensity`) of itself and `intensity` of the streak, multiplied by `tint` normalised to unit luminance, so streaks move light, never add it. With no threshold, everything streaks a little and only what is bright shows, as with bloom.
- `Camera` gains `anamorphicStreaks { intensity, length, tint }`, off by default (intensity 0, which adds no pass). A view whose bloom is off still streaks: the chain then runs bloom's first downsample for itself.
- The C# binding is regenerated from the header (`packages/FenrirRenderer/Scripting/api/`, `Managed/Fenrir.Renderer/Generated/`).
- LightingDemo's panel gains the streaks' intensity and length; the neon city turns them on, tinted blue.

**Blocked by:** 26

**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `packages/FenrirRenderer/Managed/`, `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] CPU unit tests: the streak levels' weights sum to 1 for every length, as `BloomLevelWeights` does; the normalised tint has unit luminance.
- [ ] Null-backend probes: a view with streaks off adds no pass; with them on, its passes follow bloom's in `PostProcess`, and without bloom it runs one downsample of its own.
- [ ] GL pixel probes: a bright point's streak reaches further horizontally than vertically by a wide margin; the frame's total energy, read back, is unchanged within the tolerance bloom's test uses.
- [ ] A golden image of an emissive point over black with streaks on.

Hand check: in the neon city, signs and headlights throw long, thin horizontal streaks in the tint, and dim surfaces show none.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
