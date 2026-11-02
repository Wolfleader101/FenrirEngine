# 28: Lens flare and lens dirt

**What to build:** A screen-space lens flare and an optional lens-dirt texture, in `PostProcess` after the streaks (ADR-0057). Requested by the user while tuning ticket 20 against cyberpunk reference renders, 2026-11-02.

- **Ghosts and a halo** after John Chapman's pseudo lens flare: from a small bloom level (level 1 or 2), `ghostCount` ghosts sampled along the line through the view's centre, each fading with its distance from the centre, and a halo ring sampled towards the centre at `haloRadius`; each channel offset a little, so the ghosts fringe. Blurred once with the bloom filters at that level's size, and upsampled into the composite.
- **Mixed into SceneColor as bloom is** (ADR-0057): SceneColor becomes (1 - `intensity`) of itself and `intensity` of the flare, so the flare moves light, never adds it. With no threshold, a bright sky ghosts as a veiling glare, as a real lens does; the intensity is small by design.
- **Lens dirt:** an optional texture asset over the view, scaling bloom's and the flare's share where the lens is dirty, by `dirtIntensity`, with each pixel's share held at or below 1 so the mix stays a blend. Off when no texture is set.
- `Camera` gains `lensFlare { intensity, ghostCount, ghostSpacing, haloRadius, haloWidth, chromaticOffset, dirt, dirtIntensity }`, off by default (intensity 0, which adds no pass).
- The C# binding is regenerated from the header (`packages/FenrirRenderer/Scripting/api/`, `Managed/Fenrir.Renderer/Generated/`).
- LightingDemo's panel gains the flare's intensity and the dirt's; the neon city turns on a subtle flare with a dirt texture fetched as a demo asset (ADR-0052), CC0.
- **Not here:** per-light starbursts and sprite flares placed by a light's screen position, which need a light's visibility and a sprite set; a later ticket if the neon city wants them.

**Blocked by:** 27

**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `packages/FenrirRenderer/Managed/`, `examples/LightingDemo/`

**Status:** needs-triage

- [ ] CPU unit tests: ghost positions along the line through the centre for a given count and spacing; the dirt-scaled share never exceeds 1.
- [ ] Null-backend probes: a view with the flare off adds no pass; with it on, its passes follow the streaks' in `PostProcess`.
- [ ] GL pixel probes: a bright point off-centre makes a ghost on the opposite side of the centre; a dirt texture that is black leaves bloom as it was.
- [ ] Golden images: a bright point off-centre with the flare on, and with a dirt texture made in the test (no committed demo asset).

Hand check: in the neon city, turning towards a bright sign throws faint coloured ghosts across the view and lights up the dirt on the lens.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
