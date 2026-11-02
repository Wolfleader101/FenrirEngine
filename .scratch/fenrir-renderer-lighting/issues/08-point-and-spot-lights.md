# 08: Point and spot lights, any number, and the many-lights scene

**What to build:** Two new light kinds lit by a loop over a light buffer, shaped for clustering later (spec: "Lights and the sky"; ADR-0048, ADR-0049).

- **`PointLight { colour, intensity, range, castShadows }` and `SpotLight { colour, intensity, range, innerAngle, outerAngle, castShadows }`**, `castShadows` off by default and unused until tickets 17 and 18.
- **A per-frame light buffer** in set 0 (storage, ring-buffered per frame in flight, doubling), laid out so ticket 19's clusters index it unchanged; `Fenrir/Lighting.glsl` loops over it, skipping lights whose `range` does not reach the pixel.
- **Falloff:** inverse-square windowed smoothly to zero at `range`; spot cones smooth between inner and outer angles. The glTF importer reads `KHR_lights_punctual` into these components.
- **Scene 3, many lights:** moving coloured point and spot lights over PBR surfaces, with a count slider.
- The API description gains both components (generated output only).

**Blocked by:** 07

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`, `packages/FenrirAssets/`, `examples/LightingDemo/`

**Status:** resolved

- [x] CPU unit tests: falloff at 0, mid range and `range`; cone falloff.
- [x] GL pixel probes: a point light's irradiance on a plane falls with the square of distance; nothing past `range`.
- [x] `RendererProbe` on null: every visible light reaches the buffer; a hundred lights draw.

Hand check: scene 3 looks right with a hundred lights and keeps a usable frame rate.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-31, stacked on ticket 07's branch (#266 not yet merged). Decisions to review:

- **`Core/PunctualLights`** holds the light buffer's record (`GpuLight`, 64 bytes std430: position, range, colour x intensity, kind, direction, cone scale and offset) and the CPU mirror of the falloff. A point light's cone scale 0 and offset 1 make the cone falloff 1, so one loop body serves both kinds; `kind` is kept for ticket 09's rect lights.
- **Set 0 binding 4** is the light buffer, a storage `BlockRing` (one per frame in flight, grown by doubling); the frame block grows to 96 bytes with `uvec4 lights` (x the count), since the buffer may be longer than the count. Ticket 19's clusters can index the records unchanged.
- **Falloff:** glTF's / Karis's 1 / d^2 x (1 - (d/range)^4)^2, d^2 held at least 1e-4 (finite on the light). The cone is smoothstep between the outer and inner angles' cosines (Filament squares a linear ramp; smoothstep is smooth at both ends). An inner angle at or past the outer is held just inside it.
- **`Fenrir/Lighting.glsl`:** the directional light's BRDF moved into `FenrirDirectLight`, which the sun and every punctual light call; the loop skips a light whose range does not reach the pixel. Intensities stay plain multipliers: a point light of intensity 1 one unit in front of a white rough face gives about 1, as the sun does.
- **What reaches the buffer:** every `PointLight` and `SpotLight` whose range, intensity and colour are not zero, points first. No frustum culling (the buffer is per frame, not per view); ticket 19 culls per cluster.
- **Defaults:** `range` 10, `innerAngle` 0 and `outerAngle` 45 degrees (glTF's), `castShadows` false and unused until 17 and 18.
- **Importer:** `ModelNode::light` (`ModelLight`: point or spot, colour, intensity, range, cone) from `KHR_lights_punctual`; a directional light is left out with a load warning (the scene has its own sun). `SpawnModel` adds the component; a light with no range in the file gets `sqrt(256 x intensity)` (where it falls to 1/256 of its light at one unit), at least 1. The C# `SpawnModel`'s recording copies both components too.
- **API description:** `PointLight` and `SpotLight` are components in `FenrirRenderer.api.toml`; `fenrir-describe-api` and `fenrir-generate-bindings` regenerated the description, the C# mirrors and the mappings (generated output only, no hand-written C#).
- **LightingDemo scene 3, Many lights:** a dark room of PBR floor tiles, walls, columns and spheres, 256 lights made at spawn (every fourth a spot), those past the count switched off by intensity 0; count slider, M (16/64/100/256), P (moving), intensities and range in the window.
- **Tests:** `Tests/PunctualLights.cpp` (CPU: falloff at 0, one unit, mid range, the range and past it, monotonic, inverse-square; cone inside, outside, halfway, monotonic, inverted angles), `Tests/Lights.cpp` (null: binding 4 and a count of 0; records' positions, colours, ranges, kinds and directions; lights that can light nothing left out; 100 lights all in the buffer and drawing, no stale handles; a moved light; SpawnModel on `lights.gltf`), `Tests/OpenGL/Lights.cpp` (intensity 1 at one unit lights about 1; 0.5, 1 and 2 units give ratios of 4; nothing past range; a spot dark outside its cone), and an importer test in FenrirAssets' `Models.cpp` on the hand-written `fixtures/models/lights.gltf`. `SkyLighting.cpp` and `Sky.cpp` expect set 0's fifth entry.

Hand check still to do, in LightingDemo: scene 3 with a hundred lights, and its frame rate.
