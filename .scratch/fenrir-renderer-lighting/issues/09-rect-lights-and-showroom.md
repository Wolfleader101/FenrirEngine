# 09: Rect lights through linearly transformed cosines, and the showroom scene

**What to build:** A fourth light kind with exact-looking GGX highlights from a rectangle (spec: "Lights and the sky"; ADR-0048).

- **`RectLight { colour, intensity, width, height, twoSided, range }`**, facing its entity's -Z, no shadows.
- **LTC shading** (Heitz et al. 2016) for diffuse and specular, from two 64×64 lookup tables committed as a generated header, with the reference data's licence checked and recorded.
- **Rect lights join the light buffer** and the loop.
- **Scene 4, the showroom:** a dark room, a polished floor whose roughness varies across it, a neon strip, a panel and a two-sided sign, the helmets on plinths, with ImGui controls for each light's size, intensity and sidedness.
- The API description gains the component (generated output only).

**Blocked by:** 08

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `examples/LightingDemo/`

**Status:** resolved

- [x] CPU unit tests: the LTC diffuse integral of a rect light against numerical integration for a few orientations.
- [x] GL pixel probes: a one-sided light lights nothing behind it; a two-sided one does.
- [x] Golden image: the showroom floor's reflections.

Hand check: the polished floor reflects each rect light as a shape that blurs with roughness.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-31, stacked on ticket 08's branch (#265, #266 and #267 not yet merged). Decisions to review:

- **The tables** are the reference fit's own, `fit/results/ltc_1.dds` and `ltc_2.dds` of selfshadow/ltc_code at `5c0770b7`, SHA-256 checked and copied texel for texel as half floats into the committed header `src/Core/LtcTables.hpp` by `Resources/Ltc/generate_ltc_tables.py` (the build never runs it). Their licence is BSD-style (copyright kept, paper cited), copied to `Resources/Ltc/LICENSE.txt`, with the source and checks in `Resources/Ltc/README.md`. Fitting our own tables was considered and left: the reference data is licensed for this.
- **The clipless form** (Hill and Heitz 2016, as three.js, HDRP and LearnOpenGL use it): each edge's vector form factor summed, the horizon clipped by the sphere approximation from the second table's alpha, rather than clipping the quad to the horizon. It is exact above the horizon and about 12% (0.002 absolute) high for a light straddling it; the CPU test states that bound.
- **The amplitude** of the specular is the split sum's (f0 x scale + bias from our BRDF table, times the multi-scatter compensation), not the fit's norm and Fresnel channels, so a rect light's specular matches the BRDF every other light uses; the diffuse takes 1 minus it, times 1 - metallic. Those two channels of the second table are committed but unread.
- **The record** grows from 64 to 80 bytes for every light: `halfWidth` (with `coneOffset`) and `halfHeight` (with `twoSided`) after the spot fields; `kind` 2 is a rect. Rect lights go after the points and spots in the same buffer, so ticket 19 clusters one list. The window is (1 - (d / range)^4)^2 with d to the rectangle's nearest point, and no 1 / d^2: the integral already falls with distance.
- **Set 0** gains `fenrirLtcMatrix` and `fenrirLtcAmplitude` at bindings 5 and 6 (RGBA16F, 64 x 64, written once at start-up, sampled clamped and bilinear), read with `textureLod` since the light loop is not uniform control flow. Set 0 now has seven entries and five textures; tests counting them were updated (Sky, SkyLighting, Lights, scripting's Drawing, the shader compiler's Flattening: 14 bindings).
- **`RectLight { colour, intensity, width = 1, height = 1, twoSided = false, range = 10 }`**: width and height in world units along the entity's world X and Y (its scale does not stretch it), facing its world -Z, under a mirroring parent too (its width axis is turned round so the corners wind the right way); intensity is radiance, so a white one of intensity 1 filling a white rough face's view lights it about 1, as the sky does. Not drawn itself.
- **LightingDemo scene 4, Showroom**: a dark room, a floor whose roughness rises from 0.02 to 0.5 across it with the square of the distance (a generated metallic-roughness ramp), a neon strip and a panel on the back wall, a two-sided sign hung side on, each with a glowing slab of its size; the fetched helmets on plinths (bare plinths and the missing-assets message without them). The window has each light's width, height, intensity and sidedness; Z steps every light's size (half, as set, double), V flips their sidedness.
- **API description:** `RectLight` is a component in `FenrirRenderer.api.toml`; `fenrir-describe-api` and `fenrir-generate-bindings` regenerated the description, the C# mirror and the mappings.
- **Tests:** `Tests/RectLights.cpp` (CPU: the record; the tables' size, straight-on symmetry and unclipped overhead sphere; LTC diffuse against a 400 x 400 numerical integral for five orientations within 0.2%; a light straddling the horizon within 0.005, and one wholly below it; one-sided from behind; two-sided from behind; the window), `Tests/Lights.cpp` (null: bindings 5 and 6; a rect light's record under a turned, scaled parent after a point light; under a mirroring parent; zero-sized rects left out), `Tests/OpenGL/RectLights.cpp` (a rect filling a white wall's view lights it about 1; one-sided lights nothing behind it; two-sided lights it as its front does; nothing past range; the golden image `showroom-floor.png`: three floor strips of roughness 0.05, 0.25 and 0.45 reflecting a panel, a neon strip and a two-sided sign).

- **Review (code-review skill) acted on:** an `INFO` on every check and one behaviour per test case; GLSL `FenrirLightPoint`, `FenrirLightSpot` and `FenrirLightRect` for the record's kind; named `m_ltcMatrix` and `m_ltcAmplitude`; the mirroring-parent case. Left as they are: the core struct `Render::RectLight` sharing the component's name (as the facade reads it, in another namespace); the specular LTC path checked by the golden image only, not against a CPU integral; a showroom light's slab glowing on both faces when one-sided.

Hand check done on OpenGL (RTX 5080), with the helmets fetched: the showroom floor reflects the panel as a sharp rectangle where it is glossy and the neon strip as a thin line that spreads into a broad glow where it is rougher (`.scratch/fenrir-renderer-lighting/screenshots/09-showroom.png`). The first ramp, linear from 0.02 to 0.6, left everything in view at 0.2 or rougher, so it now rises with the square of the distance across.
