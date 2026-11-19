# 09: Port the mesh programs to Slang: `mesh.vert`, unlit, lit, fallback, depth prepass and shadow

**What to build:** the programs every material draws with move to Slang on tickets 06 and 07's modules: `mesh.vert` and `unlit.frag`, `lit.frag`, `fallback.frag`, `prepass.frag`, `shadow.frag` (about 320 lines, plus the modules they import), with their keys `ALPHA_MASK`, `SKINNED` and `POINT_SHADOW` (ADR-0024, ADR-0035, ADR-0048, ADR-0049, ADR-0050, ADR-0051, ADR-0057). This is where the research's traps land in shipped programs: the layer write (2), the indirection binding (4) and the invariant position (5); tickets 03, 05 and 06 fixed each, and this ticket proves it on the real device.

- **`mesh.vert.slang`:** the vertex entry point over `Fenrir.Vertex`, `Fenrir.Draw`, `Fenrir.View` and, under `SKINNED`, `Fenrir.Skinning`, returning a struct with `SV_Position`, the five varyings at `[vk::location]` 0 to 4 (world position, normal, colour, UV, tangent) and, under `POINT_SHADOW`, `SV_RenderTargetArrayIndex` from `CurrentDraw()`'s face. The depth prepass and the shadow programs run this stage behind their own fragment stages, so its outputs are what `prepass.frag.slang` reads: the world normal at 1 and, for a `Masked` material, colour at 2 and UV at 3.
- **`lit.frag.slang`:** `MaterialParams` with `[FenrirDefault]`s, the five textures with `[FenrirDefaultTexture]`s, the tangent frames (`StreamFrame`, `DerivativeFrame` with `ddx`/`ddy`), and `surface.LitColour()`; `unlit.frag.slang`, `fallback.frag.slang`, `prepass.frag.slang` (octahedral normal through `Fenrir.Normals`, the `ALPHA_MASK` cut), `shadow.frag.slang` as straight ports. Every name in the shaders stays as it was (ADR-0073).
- **Checks on the device:** the `Draws.cpp`, `Materials.cpp`, `NormalMaps.cpp`, `Lights.cpp`, `Shadows.cpp` (cascades, spot and point: the layer write), `Skinning.cpp`, `Instancing.cpp`, `InstanceLists.cpp` (the indirection buffer, culled and direct) and `DepthPrepass.cpp` (the equal test: the invariant position) GL tests, and every golden, pass unchanged. The `UserSkinned`, `SampledPicture` and `GlFourTextures` test programs, which share `mesh.vert`, move with it (their fragment stages stay GLSL until ticket 11, which is what the mixed-program path is for).
- **ADR-0024's amendments:** `builtin:lit`'s and `builtin:unlit`'s parameters and defaults are unchanged; the C# `Shaders` facade reads the same table (`packages/FenrirAssets/Scripting/Tests`).
- **Docs:** `packages/FenrirRenderer/README.md`'s "User shaders" paragraph now describes a Slang user shader first (`import Fenrir.Lighting;`, the varyings `mesh.vert.slang` writes, the attributes), with the GLSL form as "until ADR-0073's move completes".

**Blocked by:** 08 (Port the fullscreen and post programs).

**Touches:** `packages/FenrirRenderer/Shaders/Builtin/`, `packages/FenrirRenderer/CMakeLists.txt`, `packages/FenrirRenderer/Tests/CMakeLists.txt`, `packages/FenrirRenderer/README.md`

**Status:** ready-for-agent

- [ ] Every GL test and golden under `packages/FenrirRenderer/Tests/OpenGL/` passes with no tolerance or golden changed; point shadows render into all six faces; the depth prepass's equal test leaves no holes; culled instance lists draw the right copies.
- [ ] `FenrirAssetsTests` and the C# shader fixture read `builtin:lit`'s and `builtin:unlit`'s parameters and defaults as before.
- [ ] The non-`SKINNED` variants of every mesh program bind `fenrirVisible` at GL point 27 (a tool test over the built-in containers).
- [ ] LightingDemo and RendererDemo run with every scene drawn as before (user check by eye, noted in the ticket).
