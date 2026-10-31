# 09: Rect lights through linearly transformed cosines, and the showroom scene

**What to build:** A fourth light kind with exact-looking GGX highlights from a rectangle (spec: "Lights and the sky"; ADR-0048).

- **`RectLight { colour, intensity, width, height, twoSided, range }`**, facing its entity's -Z, no shadows.
- **LTC shading** (Heitz et al. 2016) for diffuse and specular, from two 64×64 lookup tables committed as a generated header, with the reference data's licence checked and recorded.
- **Rect lights join the light buffer** and the loop.
- **Scene 4, the showroom:** a dark room, a polished floor whose roughness varies across it, a neon strip, a panel and a two-sided sign, the helmets on plinths, with ImGui controls for each light's size, intensity and sidedness.
- The API description gains the component (generated output only).

**Blocked by:** 08

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] CPU unit tests: the LTC diffuse integral of a rect light against numerical integration for a few orientations.
- [ ] GL pixel probes: a one-sided light lights nothing behind it; a two-sided one does.
- [ ] Golden image: the showroom floor's reflections.

Hand check: the polished floor reflects each rect light as a shape that blurs with roughness.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
