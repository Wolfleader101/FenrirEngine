# 12: Matching opaque, masked and shadow items merge into instanced draws

**What to build:** Consecutive items with the same pipeline, material and mesh become one instanced draw (spec: "Instancing"; ADR-0051).

- **Merging** in `DepthPrepass` and `Opaque` (and every shadow pass as they land); transparent items stay single.
- **`Fenrir/Draw.glsl`** finds a record at the base instance plus the instance index, written per backend; the draw record is unchanged, so skinned items merge too.
- **`RendererStats`** counts draws and instances apart; **`RendererConfig`** can turn merging off.
- The API description gains the stats fields (generated output only).

**Blocked by:** 10

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/Scripting/api/`

**Status:** resolved

- [x] `RendererProbe` on null: a thousand identical cubes become one draw of a thousand instances; a different material splits them; transparent items stay single; turning merging off restores one draw each.
- [x] GL pixel probes: merged cubes land where their records say; skinned merged items keep their own palettes.

Hand check: AnimationDemo's crowd draws as a handful of draws.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-11-01, stacked on ticket 11's branch (#265 to #270 not yet merged). Decisions to review:

- **The per-backend index is a specialisation constant the compiler sets.** `Fenrir/Draw.glsl`'s `CurrentDraw()` reads
  record `gl_InstanceIndex + (fenrirInstanceIndexExcludesBase ? gl_BaseInstance : 0)`. The constant (`constant_id`
  61440, reserved) defaults to false, which is right for the Vulkan SPIR-V, where the instance index counts from the
  base. `RewriteForGl` retargets `InstanceIndex` to GL's `InstanceId` (as it already did `VertexIndex`) and flips the
  constant's default to true, so the GL backend still specialises nothing. `gl_InstanceIndex` stays refused outside
  `Fenrir/Draw.glsl`; the error now points at `CurrentDraw()`.
- **Runs, not a new draw list.** Each pass turns its order into runs (`SceneRenderer::RunsOf`): the next record with
  the same pipeline (the prepass pipeline in `DepthPrepass`), material, set 2 group and mesh joins the run. The sort key
  already puts them together, and records are the draw-list positions, so a run's instances are consecutive records.
  Transparent items are always runs of one.
- **Stats:** `RendererStats::draws` now counts draws in each view's `Opaque` and `Transparent` passes (a merged run is
  one), and the new `instances` counts items, which is what `draws` used to count. `skinnedDraws` is now documented as
  a part of `instances`; I kept its name, which it had while counting items before, rather than rename a C# field
  the ticket did not ask about. The prepass draws are not counted.
- **`RendererConfig::automaticInstancing = true`**, set at install only. Ticket 13's panel switch will need a runtime
  setter.
- AnimationDemo's Crowd text now shows draws, instances and skinned.
- No shadow passes exist yet: tickets 15, 17 and 18 should build their passes' runs with `RunsOf` too. A shadow pass
  with its own pipelines will need `RunsOf` to take its pipeline per draw rather than the `prepass` flag.
- The `gl_InstanceIndex` exemption matches any include whose path ends in `/Fenrir/Draw.glsl`.
