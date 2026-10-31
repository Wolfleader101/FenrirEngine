# 22: C# reaches the lighting family

**What to build:** Everything the C++ tickets added, mirrored in C# through the generated API pipeline (spec: "Tickets"; ADR-0042 to ADR-0046).

- **Components:** `PointLight`, `SpotLight`, `RectLight`, `InstancedMeshRenderer`, the new `DirectionalLight`, `MeshRenderer` and `Camera` fields (`exposure`, `tonemapper`, ambient occlusion, bloom).
- **`Sky`:** `PanoramaSky`, and `intensity` and `rotation` on `CubemapSky`; **`AmbientLight`'s deprecated mirror is removed**.
- **Instance lists** as an asset kind C# can make and edit.
- **`RendererStats`'** draw and instance counts; `RendererConfig`'s shadow budgets where scripting reaches config.
- A C# scene in LightingDemo placing lights and an instance list.

**Blocked by:** 21

**Touches:** `packages/FenrirRenderer/Managed/`, `packages/FenrirRenderer/Scripting/`, `packages/FenrirAssets/Managed/`, `packages/FenrirAssets/Scripting/`, `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] Scripting tests: each component and field round-trips between C# and C++; an instance list made in C# draws.

Hand check: the C# scene matches its C++ counterpart.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
