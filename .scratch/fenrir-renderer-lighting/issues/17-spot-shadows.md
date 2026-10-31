# 17: Spot lights cast shadows from a shared atlas

**What to build:** Spot shadows within a budget (spec: "Shadows"; ADR-0050).

- **`SpotLight`'s `castShadows`**; **`RendererConfig`'s spot budget** (8 by default), filled by distance to the view each frame, the rest unshadowed and warned about once.
- **A 2D atlas** pooled by the budget, each light a tile with its own projection; the same filtering and biases as the sun.
- LightingDemo's many-lights scene gains spot shadows and an atlas debug view.

**Blocked by:** 08, 15

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] `RendererProbe` on null: the budget picks the nearest lights; changing it resizes the atlas.
- [ ] GL pixel probes: a spot shadow lands where its cone says.

Hand check: spot shadows hold as lights move in and out of the budget without popping badly.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
