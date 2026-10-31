# 20: The neon city at night

**What to build:** The lighting showcase (spec: "LightingDemo").

- **Scene 7:** procedural instanced buildings drawn by a user **facade shader** that lights windows by per-instance colour and lights through `Fenrir/Lighting.glsl`; Poly Haven asphalt (fetched) with a puddle mask lowering roughness; rect-light neon signs with emissive meshes to match, reflected in the wet streets through LTC specular; point-light street lamps; spot-light car headlights on instanced cars; a Poly Haven night panorama at low intensity; bloom, GTAO and AgX.
- Hundreds of lights, clustered.

**Blocked by:** 06, 11, 13, 19

**Touches:** `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] LightingDemo's smoke runs pass on null with no fetched assets.

Hand check: neon streaks on wet streets, glowing signs, and a smooth frame rate flying down the streets.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
