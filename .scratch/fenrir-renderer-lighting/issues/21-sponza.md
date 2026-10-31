# 21: Sponza, the flagship scene

**What to build:** Everything at once in a real scene (spec: "LightingDemo").

- **Scene 8:** Khronos Sponza (fetched) under a Poly Haven sky, with the sun's cascades, shadowed point lights (torches), GTAO, bloom and the tonemapper, flown around on the free-fly camera.
- The README collects every scene's hand checks.

**Blocked by:** 11, 18, 19

**Touches:** `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] LightingDemo's smoke runs pass on null with no fetched assets.

Hand check: Sponza looks right from the floor and the galleries, with stable shadows and no light leaks.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
