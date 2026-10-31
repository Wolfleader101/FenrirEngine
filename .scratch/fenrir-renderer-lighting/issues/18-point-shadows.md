# 18: Point lights cast shadows from a cubemap array

**What to build:** Point shadows within a budget, without geometry shaders (spec: "Shadows"; ADR-0050).

- **`PointLight`'s `castShadows`** and **`RendererConfig`'s point budget** (4 by default), filled as for spot lights.
- **A cubemap array** pooled by the budget, far plane at the light's `range`, depth as linear distance.
- **Six faces without a geometry shader:** as instances writing `gl_Layer` from the vertex stage where supported, else one draw per face.
- LightingDemo's many-lights scene gains point shadows.

**Blocked by:** 17

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] `RendererProbe` on null: each shadowed point light draws its casters into six faces, as instances where supported.
- [ ] GL pixel probes: a caster's shadow on each side of a point light.

Hand check: no seams at cube face edges.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
