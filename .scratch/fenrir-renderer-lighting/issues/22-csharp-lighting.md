# 22: C# reaches the lighting family

**What to build:** Everything the C++ tickets added, mirrored in C# through the generated API pipeline (spec: "Tickets"; ADR-0042 to ADR-0046).

- **Components:** `PointLight`, `SpotLight`, `RectLight`, `InstancedMeshRenderer`, the new `DirectionalLight`, `MeshRenderer` and `Camera` fields (`exposure`, `tonemapper`, ambient occlusion, bloom).
- **`Sky`:** `PanoramaSky`, and `intensity` and `rotation` on `CubemapSky`; **`AmbientLight`'s deprecated mirror is removed**.
- **Instance lists** as an asset kind C# can make and edit.
- **`RendererStats`'** draw and instance counts; `RendererConfig`'s shadow budgets where scripting reaches config.
- A C# scene in LightingDemo placing lights and an instance list.

**Blocked by:** 21

**Touches:** `packages/FenrirRenderer/Managed/`, `packages/FenrirRenderer/Scripting/`, `packages/FenrirAssets/Managed/`, `packages/FenrirAssets/Scripting/`, `examples/LightingDemo/`

**Status:** resolved

- [x] Scripting tests: each component and field round-trips between C# and C++; an instance list made in C# draws.

Hand check: the C# scene matches its C++ counterpart.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

Built 2026-11-02, stacked on ticket 21's branch (#283; #265 to #283 not yet merged). Decisions to review:

- **Most of the mirror was there already.** Each C++ ticket regenerated FenrirRenderer's binding when it changed a
  header (CI fails on a stale one), so `PointLight`, `SpotLight`, `RectLight`, `InstancedMeshRenderer`, the
  `DirectionalLight`, `MeshRenderer` and `Camera` fields, `PanoramaSky`, `CubemapSky`'s `intensity` and `rotation`,
  `SkyLight` and every `RendererStats` count were generated mirrors before this ticket. What it adds is the proof:
  `Scripting/Tests/Lighting.cpp` round-trips each component and each kind of `Sky` and `SkyLight` both ways through the
  ECS with every field off its default, compared field by field with `Iris::FieldDifferences`; C# reads the renderer's
  `RendererStats` whole, and a `RendererStats` of eight distinct counts crosses to C# and back unchanged (a null frame
  leaves the skinning counts zero, so the live read alone could not catch a misplaced one).
- **No generator change was needed.** The instance lists' natives take and give spans of `Matrix4x4` and `Colour`,
  which the natives pipeline already describes, so `Assets.AddInstanceList`, `ReplaceInstanceList`,
  `GetInstanceTransforms` and `GetInstanceColours` are generated from their C++ definitions in
  `FenrirAssets/Scripting/src/AssetsScripting.cpp`; the hand-written part adds the overloads with no owner and no
  colours. Their tests are FenrirAssets's (`Scripting/Tests/InstanceLists.cpp`), and drawing one is FenrirRenderer's.
- **The shadow budgets are `Renderer.SetSpotShadowBudget` and `SetPointShadowBudget`.** Scripting never reaches
  `RendererConfig` (the app installs the renderer before scripting), so C# gets the setters C++ has for changing the
  budgets later. C++ has no getters, so C# has none. Not added: `SetClusteredLights` and `SetAutomaticInstancing`,
  which the ticket does not ask for.
- **`AmbientLight` is gone from C#**, with its hand-written stand-in (`DeprecatedAmbientLight`) and its test. A script
  that still names it no longer compiles; the `Obsolete` message pointed at `ColourSky` since ticket 04.
- **The C# scene is Many lights (C#)**, the twin of Many lights, after Sponza, with scripting only: the same room,
  lights and paths from `scripts/ManyLights.cs`, but the markers are one instance list C# rewrites each frame (the
  first `count` lights' markers, tinted per copy) instead of a sphere entity each, so the scene places lights and an
  instance list as the ticket asks. V steps the budgets (8/4, 16/8, 0/0) through the new setters; C (clustering) is
  C++'s only, so the twin has no C. C# has no ImGui, so the twin also lacks Many lights' sliders (count, intensities,
  range, budgets): its count is M's and the rest are Many lights' starting values. The Lighting window stays C++'s (`scene.update`), as in every scene. Its hand
  checks are in the README's Hand checks section.

