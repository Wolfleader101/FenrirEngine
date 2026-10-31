# 08: Point and spot lights, any number, and the many-lights scene

**What to build:** Two new light kinds lit by a loop over a light buffer, shaped for clustering later (spec: "Lights and the sky"; ADR-0048, ADR-0049).

- **`PointLight { colour, intensity, range, castShadows }` and `SpotLight { colour, intensity, range, innerAngle, outerAngle, castShadows }`**, `castShadows` off by default and unused until tickets 17 and 18.
- **A per-frame light buffer** in set 0 (storage, ring-buffered per frame in flight, doubling), laid out so ticket 19's clusters index it unchanged; `Fenrir/Lighting.glsl` loops over it, skipping lights whose `range` does not reach the pixel.
- **Falloff:** inverse-square windowed smoothly to zero at `range`; spot cones smooth between inner and outer angles. The glTF importer reads `KHR_lights_punctual` into these components.
- **Scene 3, many lights:** moving coloured point and spot lights over PBR surfaces, with a count slider.
- The API description gains both components (generated output only).

**Blocked by:** 07

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `packages/FenrirAssets/`, `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] CPU unit tests: falloff at 0, mid range and `range`; cone falloff.
- [ ] GL pixel probes: a point light's irradiance on a plane falls with the square of distance; nothing past `range`.
- [ ] `RendererProbe` on null: every visible light reaches the buffer; a hundred lights draw.

Hand check: scene 3 looks right with a hundred lights and keeps a usable frame rate.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
