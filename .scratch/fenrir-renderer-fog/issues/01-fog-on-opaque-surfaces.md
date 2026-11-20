# 01: Fog on opaque surfaces

Spec: [Plain fog](../spec.md).

**What to build:** A scene with a `Fog` scene resource is fogged; one without is not. The resource sits beside `Sky` and `SkyLight` (distance density, start distance, height density, base height, height falloff, maximum opacity, colour from the sky or a set linear colour, colour intensity) and is described for the API description. The shared shader maths gives the fog amount along a view ray (distance fog past the start distance, the closed-form integral of exponential height fog with the near-horizontal case handled, combined and capped) and the fog colour (the sky's irradiance spherical harmonics in the view direction, scaled, or the set colour). A renderer-core fog pass in every camera view, after the background and before transparents, blends the fog colour over scene colour by the fog amount read from scene depth, leaves the sky's far-depth pixels alone, and runs per sample under MSAA, reading the multisampled depth with no resolve. The frame's uniforms carry the fog values.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

Bottom of the stack 01, 02, 04; 03 branches from it.

- [ ] CPU maths tests, beside the BRDF and spherical-harmonics tests: distance fog; the height fog's closed form against a numerical march at many angles, near-horizontal included; the combination and cap; the sky colour from harmonics.
- [ ] Null-backend tests: no fog pass without `Fog`; with it, the pass runs after the background and before transparents in every camera view, per sample with MSAA on.
- [ ] The fog values reach the frame's uniforms.
- [ ] The API description is current; the renderer's tests pass unchanged without `Fog`.
