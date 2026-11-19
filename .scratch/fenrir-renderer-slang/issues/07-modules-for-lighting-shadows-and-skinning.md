# 07: Modules for lighting, shadows and skinning: `Fenrir.Brdf`, `Fenrir.LightRecord`, `Fenrir.Clusters`, `Fenrir.SphericalHarmonics`, `Fenrir.Shadows`, `Fenrir.Lighting`, `Fenrir.SkinMaths` and `Fenrir.Skinning`

**What to build:** the lighting and skinning includes become modules on top of ticket 06's, so a material's fragment stage can `import Fenrir.Lighting;` and a vertex stage `import Fenrir.Skinning;` (ADR-0048 and ADR-0050 amended: "one shared lighting include" becomes one module). The GLSL includes stay beside them until ticket 12, and the maths is carried over line for line: the same formulas, constants and sampling, so a ported material's goldens hold.

- **`Fenrir.Lighting`** (`Lighting.slang`, 424 lines today): `FenrirSurface` as a `public struct` with its fields as the GLSL declares them, and `FenrirLitColour` as a method on it (`surface.LitColour()`) with the free function kept as a one-line wrapper for the port; the sky's spherical harmonics and prefiltered specular, the BRDF LUT and multi-scatter compensation, specular anti-aliasing, the clustered light loop over `Fenrir.Clusters` and `Fenrir.LightRecord`, the rect lights' LTC, the shadows through `Fenrir.Shadows`, the occlusion texture and the reflection blend (ADR-0056) through `Fenrir.View`. Screen-space derivatives (`ddx`, `ddy`) replace `dFdx`, `dFdy`; `texture(s, uv)` becomes `s.Sample(uv)`, `textureLod` `SampleLevel`, `texelFetch` `Load`, `texture(shadow, xyz)` `SampleCmp`. No `IFenrirMaterial` interface (ADR-0073, "Not yet").
- **`Fenrir.Shadows`**, **`Fenrir.Brdf`**, **`Fenrir.LightRecord`**, **`Fenrir.Clusters`**, **`Fenrir.SphericalHarmonics`**: straight ports with their constants from `Fenrir.Constants`.
- **`Fenrir.SkinMaths`** and **`Fenrir.Skinning`**: the palette read from `Fenrir.Draw`'s third storage block, `FenrirSkinPosition`, `FenrirSkinNormal`, `FenrirSkinTangent` over `Fenrir.Vertex`'s joints and weights, as the compute skinning (ADR-0050) and the `SKINNED` vertex path share them today.
- **Numerical parity, before any built-in is ported:** a tool-level fixture pair, `lighting_probe.frag` and `lighting_probe.frag.slang`, each a fullscreen fragment stage that lights a fixed `FenrirSurface` from fixed set 0 and set 1 data and writes the result, is built into the GL tests (`packages/FenrirRenderer/Tests/OpenGL/`) and drawn into a small target on the real device; the two images are compared under ADR-0049's golden tolerance with a tighter `deltaE`. This is the one place the port's maths is checked in isolation from the materials.
- **Docs:** ADR-0048 and ADR-0050 amendment lines; `packages/FenrirRenderer/README.md`'s "User shaders" paragraph gains the Slang form (`import Fenrir.Lighting;`, `surface.LitColour()`) beside the GLSL one until ticket 12.

**Blocked by:** 06 (Modules for the sets, the draw and the vertex).

**Touches:** `packages/FenrirRenderer/Shaders/include/Fenrir/*.slang`, `packages/FenrirRenderer/Tests/OpenGL/`, `packages/FenrirRenderer/Tests/CMakeLists.txt`, `docs/adr/0048-*.md`, `docs/adr/0050-*.md`, `packages/FenrirRenderer/README.md`

**Status:** ready-for-agent

- [ ] Every lighting and skinning module compiles into a fixture stage with no warning, and its bindings equal the GLSL include's.
- [ ] `lighting_probe` drawn through GLSL and through Slang matches within the GL tests' tolerance over a grid of roughness, metallic and light angles (a labelled `gl` test).
- [ ] A skinned fixture vertex stage through `Fenrir.Skinning` places the same vertex as the GLSL path (the `ComputeSkinning.cpp` oracle, on the null backend's reflection and on GL).
