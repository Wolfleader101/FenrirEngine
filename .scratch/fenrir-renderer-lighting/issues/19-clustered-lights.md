# 19: Lights are clustered per view

**What to build:** Each pixel loops over only the lights that reach its cluster (spec: "The frame"; ADR-0049).

- **A compute pass per view** assigns point, spot and rect lights by `range` to 16×9×24 view-space clusters with exponentially spaced depth slices, into per-view light index lists in set 1.
- **`Fenrir/Lighting.glsl` loops over its pixel's cluster**; the light buffer is unchanged, so materials do not change.
- **A cluster-heat debug view** in LightingDemo.

**Blocked by:** 09

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] CPU unit tests: cluster bounds and light-sphere tests against brute force.
- [ ] GL pixel probes: clustered and unclustered lighting agree for a scene of many lights.
- [ ] `RendererProbe` on null: a thousand lights cluster per view.

Hand check: the many-lights scene holds its frame rate at a thousand lights.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
