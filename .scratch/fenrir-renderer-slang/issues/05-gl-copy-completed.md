# 05: The GL copy is completed: draw-parameter extension, layer capability, invariant position, and `spirv-val` in the tests

**What to build:** `GlRewrite` finishes what Slang's SPIR-V 1.0 leaves out, so every GL copy the tool writes is valid SPIR-V 1.0 for OpenGL 4.6 (ADR-0073, "SPIR-V 1.0 stays the GL output, and the tool completes it"). The prototype found three gaps (research §2.3, traps 2, 3 and 5): `OpCapability DrawParameters` without `OpExtension "SPV_KHR_shader_draw_parameters"`, a vertex-stage `Layer` output without `ShaderViewportIndexLayerEXT` and `SPV_EXT_shader_viewport_index_layer`, and no way to spell `invariant` on the position.

- **Extensions and capabilities:** after its decoration pass, `RewriteForGl` walks the module's `OpCapability` and `OpExtension` instructions and inserts the extension each present capability needs and lacks (`DrawParameters` → `SPV_KHR_shader_draw_parameters`), and for a `BuiltIn Layer` decoration in a vertex stage inserts `OpCapability ShaderViewportIndexLayerEXT` and `OpExtension "SPV_EXT_shader_viewport_index_layer"` when neither `ShaderLayer` nor the EXT capability is present. Insertions go where the SPIR-V logical layout puts them (capabilities first, then extensions), with ids untouched. The same completion runs on the Vulkan copy, which the same gaps make invalid for Vulkan 1.0.
- **Invariant position:** for a vertex stage, `RewriteForGl` adds `OpDecorate %position Invariant` to the variable decorated `BuiltIn Position` when absent, for both copies, because ADR-0049's `Opaque` pass tests the prepass's depth for equality and glslang's `invariant gl_Position;` has no Slang spelling. On a GLSL stage, which already has it, nothing changes. The tool refuses a `.slang` stage using `row_major` or `column_major` in source: the layout is the tool's flag (ADR-0073).
- **Validation in the tests:** SPIRV-Tools is not a dependency and stays out of the tool. The Vulkan SDK's `spirv-val`, when found (`find_program`), runs on every GL copy and every Vulkan copy the fixtures produce, with `--target-env opengl4.5` and `vulkan1.0`, as a CTest case that is skipped with a message when the SDK is absent; CI's Windows image installs the SDK's tools for it (one step in the workflow, cached). The validation is of the tool's output, so it is a test, never a build step.
- **Tests:** the `Flattening.cpp` cases run on the Slang twins of `flatten.vert` and `flatten.frag`; a `mesh.vert`-shaped Slang fixture reading `SV_StartInstanceLocation` gets the extension; one writing `SV_RenderTargetArrayIndex` gets the capability and extension; the position is `Invariant`.

**Blocked by:** 02 (The Slang front end).

**Touches:** `packages/FenrirRenderer/Tools/ShaderCompiler/src/GlRewrite.cpp`, `packages/FenrirRenderer/Tools/ShaderCompiler/src/GlRewrite.hpp`, `packages/FenrirRenderer/Tools/ShaderCompiler/src/SlangStageCompiler.cpp`, `packages/FenrirRenderer/Tools/ShaderCompiler/Tests/`, `.github/workflows/build-and-test.yml`

**Status:** ready-for-agent

- [ ] A Slang vertex stage reading the base instance has `SPV_KHR_shader_draw_parameters` in both copies and passes `spirv-val` for `opengl4.5` and `vulkan1.0`; glslang's copy is unchanged by the pass.
- [ ] A Slang vertex stage writing the layer has `ShaderViewportIndexLayerEXT` and its extension and passes both validators.
- [ ] Every vertex stage's `Position` is `Invariant` in both copies, Slang and GLSL alike.
- [ ] A `.slang` stage with `row_major` fails with a message naming the flag.
- [ ] Every fixture container's copies pass `spirv-val` in CI.
