# 10: Port the compute, debug and 2D programs to Slang: cull, skinning, light clusters, debug lines, triangles and text, and 2D

**What to build:** the remaining built-ins move to Slang: the compute programs `cull.comp` (348 lines, ADR-0057), `skinning.comp` (ADR-0050) and `light_clusters.comp` (ADR-0049), the debug programs `debug_lines.vert`, `debug_triangles.vert`, `debug_text.vert`, `debug_text.frag` and `debug.frag` (ADR-0025), and `2d.vert` and `2d.frag` with their five keys `INPUT_SRGB`, `PREMULTIPLY`, `GAMMA`, `BLUR` and `SHADOW` (ADR-0027, ADR-0060, ADR-0065). About 850 lines.

- **Compute:** `[shader("compute")] [numthreads(64, 1, 1)]` entry points taking `SV_GroupID`, `SV_GroupThreadID` and `SV_DispatchThreadID`; `atomicAdd` becomes `InterlockedAdd` on the `RWStructuredBuffer<uint>` of commands; `Fenrir.DrawRecord` for the records and indirection entries; `Fenrir.SkinMaths` and `Fenrir.Draw`'s palette for skinning; `Fenrir.Clusters`, `Fenrir.LightRecord` and `Fenrir.View` for the clusters. The `Visibility`, `Commands` and `Indirection` buffers keep their set 1 bindings and kinds (read-write storage blocks reflect as `StorageBuffer`, as the GLSL `buffer` did).
- **Debug and 2D:** straight ports; `2d.slang`'s `MaterialParams` under `BLUR` takes `[FenrirDefault]`s where the GLSL had pragmas, and `Fenrir.2D`'s `Target2D` block keeps set 0 binding 0 so `Program.cpp` still recognises a 2D program and puts its material in set 1.
- **Checks:** `LightClusters.cpp`, `Skinning.cpp` and `ComputeSkinning.cpp` (the palette oracle), the culling GL tests (`Instancing.cpp`, `InstanceLists.cpp`, the occlusion and LOD tests ADR-0057 added), `Debug.cpp`, `Overlay.cpp` and FenrirUI's GL tests, with every golden, pass unchanged. The `indirect_counts.comp` and `indirect.vert` fixtures move to Slang here, as they exercise the cull path.
- **Docs:** ADR-0050's and ADR-0057's amendment lines (from tickets 06 and 07) now name the ported programs; ADR-0025 and ADR-0027 get one line each.

**Blocked by:** 09 (Port the mesh programs).

**Touches:** `packages/FenrirRenderer/Shaders/Builtin/`, `packages/FenrirRenderer/CMakeLists.txt`, `packages/FenrirRenderer/Tests/OpenGL/fixtures/`, `packages/FenrirRenderer/Tests/CMakeLists.txt`, `docs/adr/0025-*.md`, `docs/adr/0027-*.md`

**Status:** ready-for-agent

- [ ] Every GL test and golden passes with no tolerance or golden changed; the asteroid ring in LightingDemo culls and draws as before (user check, frame time noted).
- [ ] `ComputeSkinning.cpp`'s palette oracle holds on the ported `skinning` program.
- [ ] `Shaders/Builtin/` holds only `.slang` files, and `packages/FenrirRenderer/CMakeLists.txt` names no GLSL stage.
- [ ] FenrirUI's window, image and world UI tests pass on the ported `builtin:2d`, every variant.
