# 11: Port the demos' and the tests' shaders to Slang

**What to build:** every GLSL shader outside `Shaders/Builtin/` moves to Slang: LightingDemo's six programs (`facade`, `grass`, `ground`, `sign`, `street` and their vertex stages, 906 lines), RendererDemo's `monitor` and `stripes`, AnimationDemo's `flat`, FenrirAssets' `parameters` fixture, and FenrirRenderer's test fixtures (`plain`, `sampled`, `tinted_2d`, `user`, `wrong_frame`, `fill`, `four_textures`, `position`, `fullscreen`), and FenrirShaderCompiler's remaining GLSL-only fixtures gain Slang twins where ticket 02 to 05 did not add them. These are the user shaders' examples, so each is written as a user would write one: `import Fenrir.Lighting;` and `surface.LitColour()` for the lit ones, `[FenrirDefault]`s for defaults, the varyings `mesh.slang` writes.

- **LightingDemo:** `facade.frag` (327 lines) is the largest user shader and the model for the README's "User shaders"; its port is reviewed for reading as a user's shader should, not as a transliteration. `street`'s water, `grass`'s wind and `sign`'s emissive keep their constants.
- **Test fixtures:** each fixture's purpose comment says what it checks; `wrong_frame` still declares a wrong frame layout on purpose and the debug-build check still refuses it; `user` keeps its `FANCY` key.
- **FenrirShaderCompiler fixtures:** the `errors/` GLSL fixtures that have no Slang twin yet (`loose_uniform`, which has no Slang equivalent: a global `uniform float` in Slang is a `$Globals` cbuffer, so its twin is "a global uniform outside a `ConstantBuffer`", refused by ticket 03's binding rule) get one, so ticket 12 can delete the GLSL set with the must-fail coverage intact.
- **Docs:** the three demos' READMEs and `examples/LightingDemo`'s scene comments name `.slang` files; `docs/agents` has nothing to change.

**Blocked by:** 10 (Port the compute, debug and 2D programs).

**Touches:** `examples/LightingDemo/shaders/`, `examples/RendererDemo/shaders/`, `examples/AnimationDemo/shaders/`, `examples/*/CMakeLists.txt`, `packages/FenrirAssets/Tests/fixtures/shaders/`, `packages/FenrirAssets/Tests/CMakeLists.txt`, `packages/FenrirRenderer/Tests/fixtures/`, `packages/FenrirRenderer/Tests/OpenGL/fixtures/`, `packages/FenrirRenderer/Tests/CMakeLists.txt`, `packages/FenrirRenderer/Tools/ShaderCompiler/Tests/fixtures/`

**Status:** ready-for-agent

- [ ] No `.vert`, `.frag` or `.comp` file remains anywhere in the tree except FenrirShaderCompiler's GLSL fixtures, which ticket 12 removes.
- [ ] Every renderer, assets and scripting test and every golden passes with no change to tolerances or goldens.
- [ ] LightingDemo's neon city, forest clearing and showroom, RendererDemo and AnimationDemo draw as before (user check by eye).
- [ ] Every GLSL must-fail fixture has a Slang twin that fails with the same message shape.
