# 08: C# reaches culling switches, the new statistics and levels of detail

**What to build:** C# mirrors of what tickets 03 to 06 put in the API descriptions, as the lighting spec's ticket 22 does for its fields (spec: "Culling", "Levels of detail").

- **`RendererConfig`'s `gpuCulling` and `lodErrorPixels`**, `Renderer.SetGpuCulling` and `Renderer.SetCullingFrozen`, and `RendererStats`' `instancesDrawn`, `framesLate` and `drawnByLod`, generated from the API descriptions.
- **`MeshLod`, `MeshData`'s levels and `GenerateLods`** from FenrirAssets, wherever C# already reaches `MeshData`; where it does not, generating levels from C# is left out and the README says so.
- Hand-written C# only where the generators cannot describe a shape, and then the generator change is proposed first (the repo's rule for binding-pipeline limits).

**Blocked by:** 07

**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`, `packages/FenrirAssets/Scripting/`, `packages/FenrirAssets/Managed/`, `packages/FenrirRenderer/README.md`

**Status:** ready-for-agent

- [ ] C# tests: each new field and function round-trips through the bindings, and defaults match the C++ ones (the generated defaults tests).
- [ ] A ScriptingDemo or test script turns culling off and reads `instancesDrawn`.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
