# 12: Matching opaque, masked and shadow items merge into instanced draws

**What to build:** Consecutive items with the same pipeline, material and mesh become one instanced draw (spec: "Instancing"; ADR-0051).

- **Merging** in `DepthPrepass` and `Opaque` (and every shadow pass as they land); transparent items stay single.
- **`Fenrir/Draw.glsl`** finds a record at the base instance plus the instance index, written per backend; the draw record is unchanged, so skinned items merge too.
- **`RendererStats`** counts draws and instances apart; **`RendererConfig`** can turn merging off.
- The API description gains the stats fields (generated output only).

**Blocked by:** 10

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`

**Status:** ready-for-agent

- [ ] `RendererProbe` on null: a thousand identical cubes become one draw of a thousand instances; a different material splits them; transparent items stay single; turning merging off restores one draw each.
- [ ] GL pixel probes: merged cubes land where their records say; skinned merged items keep their own palettes.

Hand check: AnimationDemo's crowd draws as a handful of draws.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
