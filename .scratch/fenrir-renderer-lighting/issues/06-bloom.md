# 06: Physically based bloom in `PostProcess`

**What to build:** Bright pixels bleed into their surroundings before the tonemap, as CoD: Advanced Warfare's bloom does (spec: "The frame"; ADR-0049).

- **A downsample chain** with Jimenez's 13-tap filter (Karis average on the first step against fireflies) and **a tent-filter upsample chain**, as render graph passes per view in `PostProcess`.
- **No threshold:** the result is mixed into `SceneColor` by `Camera`'s `bloom.intensity` (0.04 by default; 0 culls the passes) and `bloom.radius`.
- The API description gains the settings (generated output only).

**Blocked by:** 01

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`

**Status:** ready-for-agent

- [ ] `RendererProbe` on null: the bloom passes run per view, and are culled at intensity 0.
- [ ] GL pixel probes: a single bright pixel spreads symmetrically and energy is preserved within tolerance.
- [ ] Golden image: an emissive sphere with bloom.

Hand check: emissive surfaces glow softly with no hard threshold edge.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
