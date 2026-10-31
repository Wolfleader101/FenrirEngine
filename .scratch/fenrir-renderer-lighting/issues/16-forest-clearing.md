# 16: The forest clearing at golden hour

**What to build:** The dense, alpha-tested, animated instancing case (spec: "LightingDemo").

- **Scene 6:** Poly Haven CC0 trees, ferns, shrubs, rocks and logs (fetched) as about 20k instances over several instance lists; a Poly Haven ground set and a sunset panorama; a low sun with cascades.
- **About 200k procedural grass blades** (a generated mesh, no asset) through a **user wind shader** in the demo, swaying in its vertex stage and lighting through `Fenrir/Lighting.glsl`, also used by the depth prepass and shadow passes.
- **Alpha-tested leaves** exercise `Masked` in the prepass and in shadows; GTAO grounds the plants.

**Blocked by:** 11, 13, 15

**Touches:** `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] LightingDemo's smoke runs pass on null with no fetched assets, the grass drawn from its generated mesh.

Hand check: the grass sways and its shadows sway with it; leaves cut out cleanly in light and shadow.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
