# 02: Fog in transparents' shading

Spec: [Plain fog](../spec.md).

**What to build:** Transparent surfaces are fogged as the opaque ones behind them are. The shared lighting include gains a fog helper taking a world position and a shaded colour and using the frame's fog values; it returns its input when the scene has no fog. The built-in transparent materials (lit and unlit, alpha and additive) call it, additive ones fading toward no contribution rather than toward the fog colour. A game's transparent shader calls the same helper.

**Blocked by:** 01
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

Stacked on 01.

- [ ] Null-backend tests: the built-in transparent materials' shaders use the fog helper.
- [ ] With no `Fog`, transparent output is unchanged.
- [ ] A CPU test of the helper's maths for alpha and additive blending.
