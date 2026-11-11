# 09: C# reaches culling switches, the new statistics and levels of detail

**What to build:** C# mirrors of what tickets 04 to 07 put in the API descriptions, as the lighting spec's ticket 22 does for its fields (spec: "Culling", "Levels of detail").

- **`RendererConfig`'s `gpuCulling`, `lodErrorPixels` and `minProjectedPixels`**, `Renderer.SetGpuCulling`, `Renderer.SetCullingFrozen`, `Renderer.SetLodSelection` (ticket 08's, for a live `lodErrorPixels`) and `Renderer.GetFrustum` with `Math.Frustum`, and `RendererStats`' `instancesDrawn` and `drawnByLod`, generated from the API descriptions.
- **`MeshLod`, `MeshData`'s levels and `GenerateLods`** (with `LodSettings`, `lockSeams` included) from FenrirAssets, wherever C# already reaches `MeshData`; where it does not, generating levels from C# is left out and the README says so.
- Hand-written C# only where the generators cannot describe a shape, and then the generator change is proposed first (the repo's rule for binding-pipeline limits).

**Blocked by:** 08

**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`, `packages/FenrirAssets/Scripting/`, `packages/FenrirAssets/Managed/`, `packages/FenrirRenderer/README.md`

**Status:** resolved

- [x] C# tests: each new field and function round-trips through the bindings, and defaults match the C++ ones (the generated defaults tests).
- [x] A ScriptingDemo or test script turns culling off and reads `instancesDrawn`.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-11-11 on the integration branch (ticket 10's #325 had merged). Decisions to review:

- **The natives are the setters and `GetFrustum`.** `Renderer.SetGpuCulling`, `SetOcclusionCulling` (not named here, but
  the switch ticket 11 added beside the others), `SetCullingFrozen` and `SetLodSelection(lodErrorPixels, minProjectedPixels)`
  are generated from their C++ definitions in `RendererScripting.cpp`, as the shadow budgets were; `SetLodSelection` keeps
  C++'s two floats rather than the `LodSelection` struct the 07 review floated, since neither the config nor the facade has
  such a type. `RendererConfig` is still not described and scripting never reaches it (the app installs the renderer before
  scripting, ticket 22's finding), so `gpuCulling`, `occlusionCulling`, `lodErrorPixels` and `minProjectedPixels` reach C#
  only through the setters, and the renderer README says so. C++ has no getters, so C# has none.
- **`Math::Frustum` is described, not hand-written, and the describer learnt `std::array`.** The frustum is a class with
  private members (`m_planes`, a `std::array<Vec4, 6>`, and `m_count`), so FenrirMath's selection names it in
  `private-members` and the math binding keeps the fields private under `planes` and `count`. `tools/describe-api` recorded
  only C arrays as the `array` kind and described a `std::array` as a nested struct with the library's own member name
  (`_Elems` on MSVC, `_M_elems` on libstdc++, so the committed description would differ by platform): it now records a
  `std::array<T, N>` as the C array it holds, with its braced list elided or not (`SCHEMA.md`, the fixture's `pair` and
  `coils`). Iris.Bindings upper-cases an array's storage struct (`PlanesArray`), so a private field's lower-case name gives a
  C# type name. The hand-written `Frustum.cs` gives `Count`, an indexer and `Intersects` for a box, a moved box and a sphere,
  the same arithmetic as C++'s, which `Culling.cpp` compares case by case.
- **`RendererStats` needed no change**, as the notes said: `Culling.cpp` echoes every count, the two arrays' entries
  included, each with a value of its own, and reads `InstancesDrawn` through the live `Stats`.
- **Levels of detail from C# go by handle.** C# reaches `MeshData` only as the spans `AddMesh` and `ReplaceMesh` take, so
  `MeshLod` and `MeshData` have no mirror; instead `Assets.GenerateLods(mesh, settings)` replaces the mesh's data with C++'s
  `GenerateLods` (`LodSettings` is described, with `lockSeams`, and its generated defaults test holds), `AddMeshLod` appends
  a hand-built level under the store's rules, and `GetMeshLodErrors` and `GetMeshLodIndices` read the levels back. The
  FenrirAssets README's "Levels of detail" has the C# paragraph.
- **ScriptingDemo's `Culling.cs`**: G toggles `Renderer.SetGpuCulling` and, three frames later, logs
  `G: GPU culling off; N of M instances drawn.` from `Renderer.Stats` (0 of 2 on the null backend, which runs no cull
  shader; 2 of 2 on the GPU). The Showcase smoke run presses G and `CheckShowcaseLog.cmake` wants the line.
