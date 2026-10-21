# How engines write shaders once for several graphics APIs

Research for [04-shader-languages-and-toolchains](../issues/04-shader-languages-and-toolchains.md). This file reports the routes engines and toolchains take, and what each would cost FenrirRenderer if it starts on OpenGL and later adds D3D, Vulkan or Metal. It does not choose a route.

Read on 2026-10-21. Sources are the projects' own source and docs on GitHub, pinned to the commit read, plus Khronos and CMake documentation. Line numbers refer to those commits:

- glslang `main` @ [`5a0d625`](https://github.com/KhronosGroup/glslang/tree/5a0d62504b8126220a924613a902e620351d5ebf) (latest release 16.6.0, 2026-09-11)
- shaderc `main` @ [`ba3e587`](https://github.com/google/shaderc/tree/ba3e587dbc13d423c713e964ac08e094731a034d) (latest tag v2026.4)
- SPIRV-Cross `main` @ [`300681b`](https://github.com/KhronosGroup/SPIRV-Cross/tree/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1)
- SPIRV-Reflect `main` @ [`563dc9a`](https://github.com/KhronosGroup/SPIRV-Reflect/tree/563dc9a16d0e86145e12a66bb61f0695ca4a14b9)
- DirectXShaderCompiler (DXC) `main` @ [`c3a8532`](https://github.com/microsoft/DirectXShaderCompiler/tree/c3a85328eb12dffdba41026d6ec940c22c5308e0) (latest release v1.9.2609, 2026-09-29)
- Slang `master` @ [`f96c0d8`](https://github.com/shader-slang/slang/tree/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd) (latest release v2026.20, 2026-10-17)
- naga, in the wgpu repo, `trunk` @ [`3050396`](https://github.com/gfx-rs/wgpu/tree/3050396df223f189925c5a7d9d81c0ef219b6117) (wgpu v30)
- Tint, in the Dawn repo, `main` @ [`43b805e`](https://github.com/google/dawn/tree/43b805ec9edc136b42b55f77ff46f90e73ca2a07)
- bgfx `master` @ [`21a5cf8`](https://github.com/bkaradzic/bgfx/tree/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f)
- Godot `master` @ [`6790667`](https://github.com/godotengine/godot/tree/679066724352c12673001d692d41dc2dab9314c0) (latest release 4.7.2-stable)
- Filament `main` @ [`a843093`](https://github.com/google/filament/tree/a84309371cca4cbe20efd79e0033ad19849b3c59) (latest release v1.78.0)
- Bevy `main` @ [`2614884`](https://github.com/bevyengine/bevy/tree/2614884437512e310eeb196241828162b6507533) (latest release v0.20.0)
- Khronos GLSL extension specs `main` @ [`0099bf8`](https://github.com/KhronosGroup/GLSL/tree/0099bf83b0288469917fc2e2972cb0a2b74d6711); `ARB_gl_spirv` and the GL reference pages from registry.khronos.org; CMake docs 4.5.0-rc1.

Every link is a full URL. Markdown and reStructuredText files are linked with `?plain=1` so the line anchors work.

FenrirEngine context, from this worktree: the placeholder `examples/DemoApp` asks GLFW for an OpenGL 4.6 context ([Window.cpp L26–L27](../../../examples/DemoApp/src/Window.cpp)) and its shaders are `#version 460 core` GLSL loaded at runtime. ADR-0010 (as the map restates it) requires the shader compiler to be linked `PRIVATE`.

---

## Summary

| Route | Source language | What OpenGL gets | What D3D / Vulkan / Metal get | Reflection from | Main cost |
|---|---|---|---|---|---|
| 1. GLSL only | GL GLSL | the source, compiled by the driver | nothing; a translator must be added later | the GL driver (`glGetProgramResource`) | none now; a rewrite or a translator later |
| 2. GLSL → SPIR-V → SPIRV-Cross | GLSL (Vulkan flavour, or GL flavour) | GLSL emitted by SPIRV-Cross, or SPIR-V directly on GL 4.6 | SPIR-V (Vulkan), HLSL (D3D11/12 via FXC or DXC), MSL (Metal) | SPIRV-Cross or SPIRV-Reflect on the SPIR-V | glslang + SPIRV-Tools + SPIRV-Cross; all permissive, all CMake |
| 3. HLSL with DXC | HLSL | GLSL via DXC → SPIR-V → SPIRV-Cross | DXIL (D3D12), SPIR-V (Vulkan), MSL via SPIRV-Cross | DXC's D3D12 reflection, or SPIR-V reflection | a 30 MB prebuilt or an LLVM-sized source build |
| 4. Slang | Slang (HLSL-like) | GLSL (limited OpenGL support) | DXIL (through DXC), SPIR-V, MSL (experimental), WGSL (experimental) | Slang's own reflection API | 60 MB shared library; OpenGL is its weakest target |
| 5. WGSL with naga or Tint | WGSL | GLSL emitted by naga or Tint | SPIR-V, HLSL, MSL | naga IR or Tint Inspector | naga is Rust; Tint lives inside Dawn |
| 6. Custom language | engine's own | engine's own GLSL generator | via SPIR-V (Godot) or per-API back ends (bgfx) | the engine's own compiler | writing and keeping a compiler |

Points that cut across all routes:

- **glslang's HLSL front end is deprecated** as of April 2026 and will be removed at its next major version; bug reports for it are no longer accepted ([glslang README L7, L25–L31](https://github.com/KhronosGroup/glslang/blob/5a0d62504b8126220a924613a902e620351d5ebf/README.md?plain=1#L7-L31)). shaderc repeats this for its HLSL input ([shaderc README L7–L12](https://github.com/google/shaderc/blob/ba3e587dbc13d423c713e964ac08e094731a034d/README.md?plain=1#L7-L12)). Any HLSL route therefore means DXC or Slang, not glslang.
- **OpenGL 4.6 can take SPIR-V directly.** Khronos announced 4.6 as folding in "the capability to ingest SPIR-V shaders" ([Khronos press release](https://www.khronos.org/news/press/khronos-releases-opengl-4.6-with-spir-v-support)). The rules are those of `ARB_gl_spirv` (below), and they remove name-based reflection from the driver.
- **SPIR-V is the hub every multi-API toolchain read here passes through**, except bgfx's D3D paths and Godot's own front end: bgfx, Godot and Filament all compile to SPIR-V with glslang and then convert with SPIRV-Cross (and Tint for WGSL), as the per-engine sections show.

---

## Route 1: GLSL only

- OpenGL compiles GLSL source in the driver at runtime. Slang's docs call GLSL "the de facto intermediate language of OpenGL", and loading GLSL source "the most widely supported" option ([Slang 09-targets L275–L276](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/09-targets.md?plain=1#L275-L276)).
- **Reflection comes from the driver.** `glGetProgramResource` returns properties such as location, offset and buffer binding for program interfaces like uniforms, program inputs and uniform blocks; it needs GL 4.3 ([GL reference page](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGetProgramResource.xhtml)).
- **Bindings can be set in the shader or by name from C++.** `layout(binding = N)` on samplers and UBOs needs GLSL 4.20 or `GL_ARB_shading_language_420pack` (SPIRV-Cross's option comment says so: [spirv_glsl.hpp L113–L117](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/spirv_glsl.hpp#L113-L117)). Godot's GL3 back end, which targets GLES3 and GL 3.3, instead looks up names and assigns units after linking: `glGetUniformLocation` then `glUniform1i` for texture units and `glUniformBlockBinding` for UBOs ([shader_gles3.cpp L283–L300](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/drivers/gles3/shader_gles3.cpp#L283-L300)).
- **Variants are `#define`s prepended to the source.** Godot's GL3 back end builds a bitmask of "specializations" and emits `#define NAME` for each set bit ([shader_gles3.cpp L181–L182](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/drivers/gles3/shader_gles3.cpp#L181-L182)). It compiles a specialization lazily the first time it is bound, and caches it ([shader_gles3.h L185–L208](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/drivers/gles3/shader_gles3.h#L185-L208)).
- **Cost now:** no dependency at all. **Cost later:** GL GLSL is not what Vulkan GLSL, HLSL or MSL accept. `GL_KHR_vulkan_glsl` removes default (non-block, non-opaque) uniforms and adds descriptor sets, separate textures and samplers, push constants and specialization constants ([GL_KHR_vulkan_glsl, Overview](https://github.com/KhronosGroup/GLSL/blob/0099bf83b0288469917fc2e2972cb0a2b74d6711/extensions/khr/GL_KHR_vulkan_glsl.txt)). Shaders written with loose `uniform float x;` would need rewriting into blocks before any SPIR-V route could serve Vulkan. Shaders written from the start with explicit `layout(binding)`, `layout(location)` and all non-opaque uniforms in UBOs can be fed to glslang later.

## Route 2: GLSL → SPIR-V (glslang or shaderc) → SPIRV-Cross

### The pieces

- **glslang** is the Khronos reference front end for GLSL and ESSL; it builds an AST and generates SPIR-V ([glslang README L19–L23](https://github.com/KhronosGroup/glslang/blob/5a0d62504b8126220a924613a902e620351d5ebf/README.md?plain=1#L19-L23)). Its C++ API is `TShader::setEnvInput / setEnvClient / setEnvTarget`, `parse`, then `TProgram::link`; the client may be `EShClientVulkan` or `EShClientOpenGL` ([README L360–L385](https://github.com/KhronosGroup/glslang/blob/5a0d62504b8126220a924613a902e620351d5ebf/README.md?plain=1#L360-L385)). It can now be built as a shared library ([README L13](https://github.com/KhronosGroup/glslang/blob/5a0d62504b8126220a924613a902e620351d5ebf/README.md?plain=1#L13)). Its own reflector works on the AST, not on SPIR-V, and is "only approximate for what would later be emitted for SPIR-V" ([README L41–L45](https://github.com/KhronosGroup/glslang/blob/5a0d62504b8126220a924613a902e620351d5ebf/README.md?plain=1#L41-L45)).
- **shaderc** wraps glslang and SPIRV-Tools as `glslc` (a GCC-like command line) and `libshaderc` (a C API), adding `#include` support and a stable API ([shaderc README L1–L22](https://github.com/google/shaderc/blob/ba3e587dbc13d423c713e964ac08e094731a034d/README.md?plain=1#L1-L22)). It pins glslang, SPIRV-Tools and SPIRV-Headers revisions in `DEPS` ([DEPS](https://github.com/google/shaderc/blob/ba3e587dbc13d423c713e964ac08e094731a034d/DEPS)). The API has macro definitions, include callbacks, target environment, auto-binding and binding bases per resource kind and stage ([shaderc.h L310, L392, L408, L433–L477](https://github.com/google/shaderc/blob/ba3e587dbc13d423c713e964ac08e094731a034d/libshaderc/include/shaderc/shaderc.h#L310-L477)). `glslc --target-env` accepts `vulkan1.0`–`vulkan1.4` and `opengl`/`opengl4.5` (OpenGL 4.5 semantics) ([glslc README L227–L247](https://github.com/google/shaderc/blob/ba3e587dbc13d423c713e964ac08e094731a034d/glslc/README.asciidoc?plain=1#L227-L247)), and writes Make-style dependency files with `-MD`/`-MF` ([L494–L516](https://github.com/google/shaderc/blob/ba3e587dbc13d423c713e964ac08e094731a034d/glslc/README.asciidoc?plain=1#L494-L516)).
- **SPIRV-Cross** converts SPIR-V to GLSL, MSL and HLSL, or to a JSON reflection format, and has a reflection API "to simplify the creation of Vulkan pipeline layouts" ([SPIRV-Cross README L13–L22](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/README.md?plain=1#L13-L22)). It has a C++ API and a C wrapper ([L78–L197](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/README.md?plain=1#L78-L197)).
- **SPIRV-Reflect** is a smaller C/C++ reflection-only library: descriptor bindings, push-constant sizes, full UBO layouts and input/output variables ([SPIRV-Reflect README L22–L31](https://github.com/KhronosGroup/SPIRV-Reflect/blob/563dc9a16d0e86145e12a66bb61f0695ca4a14b9/README.md?plain=1#L22-L31)).

### What crossing back to GL GLSL needs

SPIRV-Cross's README lists the adjustments an app makes when the SPIR-V came from Vulkan-style source:

- **Separate textures and samplers** must be merged by calling `build_combined_image_samplers()` before compiling to GL GLSL, or it throws ([README L385–L404](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/README.md?plain=1#L385-L404)).
- **Descriptor sets** must be flattened to a single binding scheme (set 0) for GL; the README's own example rewrites `binding = set * 16 + binding` ([L106–L119, L406–L410](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/README.md?plain=1#L106-L410)).
- **Older GLSL links stages by name**, so I/O names must match ([L429–L434](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/README.md?plain=1#L429-L434)).
- **Clip space:** `fixup_clipspace` converts between `[0,w]` and `[-w,w]` depth; the README recommends changing the projection matrix instead ([L436–L447](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/README.md?plain=1#L436-L447)).
- **Specialization constants become macros.** Without Vulkan semantics, SPIRV-Cross emits `#ifndef SPIRV_CROSS_CONSTANT_ID_N / #define ... / #endif` and a `const` that reads the macro ([spirv_glsl.cpp L3276–L3293](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/spirv_glsl.cpp#L3276-L3293)). Filament's GL back end prepends `#define SPIRV_CROSS_CONSTANT_ID_<id> <value>` lines to the GLSL before compiling it ([ShaderCompilerService.cpp L771–L796](https://github.com/google/filament/blob/a84309371cca4cbe20efd79e0033ad19849b3c59/filament/backend/src/opengl/ShaderCompilerService.cpp#L771-L796)).

### The other way: hand GL the SPIR-V itself

`ARB_gl_spirv` (core in 4.6) loads SPIR-V through `ShaderBinary` with `SHADER_BINARY_FORMAT_SPIR_V_ARB` and `SpecializeShaderARB`, and needs GL 3.3 or later ([ARB_gl_spirv, Dependencies, §7.2](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_gl_spirv.txt)). Its rules matter for reflection:

- Names may be absent because `OpName` is optional, and name-based queries such as `GetUniformLocation` return -1 (§7.3.1, Issue 22).
- Interface variables need explicit `Location`, uniform and storage blocks need explicit `Binding`, and `DescriptorSet` must be 0 (§7.4, Overview).
- Subroutines and separate samplers are not available (Overview, Issue 13).

So an engine using this path gets its reflection from the SPIR-V (SPIRV-Cross or SPIRV-Reflect) rather than from the driver.

### Who uses it

- **Filament** compiles its materials offline (`matc`) through `GLSLPostProcessor`: glslang to SPIR-V, then SPIRV-Cross `CompilerGLSL` for GL, `CompilerMSL` for Metal, and Tint's SPIR-V reader and WGSL writer for WebGPU ([GLSLPostProcessor.cpp L42–L47, L413–L419, L591–L657, L964](https://github.com/google/filament/blob/a84309371cca4cbe20efd79e0033ad19849b3c59/libs/filamat/src/GLSLPostProcessor.cpp#L42-L964)).
- **bgfx** uses the same pair for its GLSL, Metal and SPIR-V profiles; see route 6.
- **Godot**'s RenderingDevice back ends start from glslang's SPIR-V; see route 6.

## Route 3: HLSL with DXC

- DXC is a fork of LLVM/Clang that compiles HLSL to DXIL for D3D12 and to SPIR-V for Vulkan ([DXC README L1–L3, L21–L23](https://github.com/microsoft/DirectXShaderCompiler/blob/c3a85328eb12dffdba41026d6ec940c22c5308e0/README.md?plain=1#L1-L23)). It ships `dxc.exe`, `dxcompiler.[dll|so]` and `dxil.[dll|so]` (validation and hashing) ([L24–L31](https://github.com/microsoft/DirectXShaderCompiler/blob/c3a85328eb12dffdba41026d6ec940c22c5308e0/README.md?plain=1#L24-L31)). Prebuilt binaries come from GitHub releases, NuGet, the Windows SDK (DXIL only) and the Vulkan SDK ([L12–L17](https://github.com/microsoft/DirectXShaderCompiler/blob/c3a85328eb12dffdba41026d6ec940c22c5308e0/README.md?plain=1#L12-L17)). Built from source, it can emit Metal libraries through Apple's Metal Shader Converter ([L47–L54](https://github.com/microsoft/DirectXShaderCompiler/blob/c3a85328eb12dffdba41026d6ec940c22c5308e0/README.md?plain=1#L47-L54)).
- **Its SPIR-V is Vulkan-only.** "At the moment we only intend to support the Vulkan flavor of SPIR-V" ([SPIR-V.rst L18–L21](https://github.com/microsoft/DirectXShaderCompiler/blob/c3a85328eb12dffdba41026d6ec940c22c5308e0/docs/SPIR-V.rst?plain=1#L18-L21)); `-fspv-target-env` takes Vulkan environments and defaults to `vulkan1.0` ([L458–L470](https://github.com/microsoft/DirectXShaderCompiler/blob/c3a85328eb12dffdba41026d6ec940c22c5308e0/docs/SPIR-V.rst?plain=1#L458-L470)). For OpenGL the path is DXC → SPIR-V → SPIRV-Cross GLSL, with the same flattening work as route 2.
- **Registers to bindings.** HLSL binds by register class and space (`b`, `t`, `u`, `s`); Vulkan by set and binding. DXC maps `register(xN, spaceM)` to binding N in set M regardless of class, which collides when two classes share a number; `-fvk-{b,s,t,u}-shift N M` shift each class apart, `-fvk-bind-register` maps registers one by one, and `[[vk::binding(X, Y)]]` sets them in source ([L145–L169, L1756–L1815](https://github.com/microsoft/DirectXShaderCompiler/blob/c3a85328eb12dffdba41026d6ec940c22c5308e0/docs/SPIR-V.rst?plain=1#L1756-L1815)).
- **Buffer layout.** SPIR-V output defaults to relaxed std140/std430; `-fvk-use-gl-layout` gives strict GL layouts so data packed for GL matches, and `-fvk-use-dx-layout` gives D3D packing ([L1025–L1049](https://github.com/microsoft/DirectXShaderCompiler/blob/c3a85328eb12dffdba41026d6ec940c22c5308e0/docs/SPIR-V.rst?plain=1#L1025-L1049)).
- **Specialization constants** are written `[[vk::constant_id(N)]] const T x = v;` ([L231–L241](https://github.com/microsoft/DirectXShaderCompiler/blob/c3a85328eb12dffdba41026d6ec940c22c5308e0/docs/SPIR-V.rst?plain=1#L231-L241)). D3D has no equivalent; they only reach Vulkan (and GL through the SPIRV-Cross macros above).
- **Reflection:** semantic strings and HLSL user types are dropped from SPIR-V unless `-fspv-reflect` is given ([L657–L685](https://github.com/microsoft/DirectXShaderCompiler/blob/c3a85328eb12dffdba41026d6ec940c22c5308e0/docs/SPIR-V.rst?plain=1#L657-L685)). For DXIL, bgfx's DXIL back end loads `dxcompiler.dll` / `libdxcompiler.so` at runtime and includes `d3d12shader.h` ([shaderc_dxil.cpp L25–L28, L218–L260](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/tools/shaderc/shaderc_dxil.cpp#L25-L260)).
- **Cost:** the v1.9.2609 release is a 32.3 MB Windows zip and a 13.3 MB Linux tarball ([release v1.9.2609](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609)); building from source is an LLVM-sized build. Licence: University of Illinois/NCSA ([README L106–L109](https://github.com/microsoft/DirectXShaderCompiler/blob/c3a85328eb12dffdba41026d6ec940c22c5308e0/README.md?plain=1#L106-L109)).

## Route 4: Slang

- Slang targets D3D11 and D3D12 (HLSL), Vulkan (SPIR-V, GLSL), Metal (MSL, experimental, vertex/fragment/compute/task/mesh only), WebGPU (WGSL, experimental), CUDA and CPU ([Slang README L105–L121](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/README.md?plain=1#L105-L121)). Support is "focused on the platforms (Windows, Linux) and target APIs (Direct3D 12, Vulkan)" ([L125–L130](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/README.md?plain=1#L125-L130)).
- **OpenGL is its weakest target.** "Slang has only limited support for compiling code for OpenGL" ([09-targets L264–L268](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/09-targets.md?plain=1#L264-L268)). Its GLSL options include `-profile glsl_<version>` and `-fvk-use-gl-layout`, and it notes that scalar layout is not expected to be usable for OpenGL ([a2-04-glsl-target-specific L64–L81](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/a2-04-glsl-target-specific.md?plain=1#L64-L81)).
- **The language** is close to HLSL (most HLSL compiles unchanged) and has a GLSL compatibility module ([README L40–L43](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/README.md?plain=1#L40-L43)). It adds modules, generics and interfaces.
- **DXIL goes through DXC.** `slangc` "may leverage a 'downstream' tool like 'dxc', 'fxc', 'glslang', or 'gcc'" ([08-compiling L310](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/08-compiling.md?plain=1#L310)), so a D3D12 target also brings DXC.
- **Reflection** is one API for every target, but only through the compilation API: "reflection information cannot be queried from code compiled via the command-line `slangc` tool" ([09-reflection L20–L27](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/09-reflection.md?plain=1#L20-L27)). Layout is multi-dimensional: a field's offset is reported in several units at once, for example one `t` register, one `s` register and 32 bytes on D3D12 ([L336–L372](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/09-reflection.md?plain=1#L336-L372)).
- **Variants:** Slang argues against preprocessor permutations and offers link-time constants and types: modules precompiled to IR once, specialization arguments supplied at link time ([10-link-time-specialization L6–L40](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/10-link-time-specialization.md?plain=1#L6-L40)). It also supports SPIR-V specialization constants (`layout(constant_id = N)` or `[SpecializationConstant]`) ([a2-04 L56–L58](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/a2-04-glsl-target-specific.md?plain=1#L56-L58)).
- **Cost:** releases ship `slangc`, a shared library and `slang.h`; Slang is also in the Vulkan SDK since 1.3.296.0 ([README L53–L56](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/README.md?plain=1#L53-L56)). The v2026.20 Windows x86_64 zip is 63.7 MB and the Linux x86_64 glibc-2.28 tarball 29.7 MB ([release v2026.20](https://github.com/shader-slang/slang/releases/tag/v2026.20)). `SLANG_LIB_TYPE` defaults to `SHARED`, the source build needs Python 3, and some configurations fetch DXC and LLVM ([building.md L14–L15, L324–L330, L376](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/building.md?plain=1#L14-L376)). Licence: Apache 2.0 with LLVM exception ([README L133–L135](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/README.md?plain=1#L133-L135)).

## Route 5: WGSL with naga or Tint

- **naga** is "the shader translation library for the needs of wgpu". It reads SPIR-V, WGSL and GLSL (440+, Vulkan semantics only) and writes SPIR-V, WGSL, MSL, HLSL (SM 5.0+) and GLSL (330+ and ES 300+) ([naga README L8–L31](https://github.com/gfx-rs/wgpu/blob/3050396df223f189925c5a7d9d81c0ef219b6117/naga/README.md?plain=1#L8-L31)). It is a Rust crate (MSRV 1.87 in its `Cargo.toml`, workspace 1.95) under MIT OR Apache-2.0 ([naga/Cargo.toml L16](https://github.com/gfx-rs/wgpu/blob/3050396df223f189925c5a7d9d81c0ef219b6117/naga/Cargo.toml#L16), [workspace Cargo.toml](https://github.com/gfx-rs/wgpu/blob/3050396df223f189925c5a7d9d81c0ef219b6117/Cargo.toml)). Its README documents a Rust library and a `naga-cli` installed with `cargo` ([L35–L54](https://github.com/gfx-rs/wgpu/blob/3050396df223f189925c5a7d9d81c0ef219b6117/naga/README.md?plain=1#L35-L54)); it documents no C API, so a C++ engine would use the CLI at build time or write its own FFI, and the build would need a Rust toolchain.
- **Tint** is Dawn's WGSL compiler, usable standalone to convert to and from WGSL ([Dawn README](https://github.com/google/dawn/blob/43b805ec9edc136b42b55f77ff46f90e73ca2a07/README.md?plain=1#L22-L23)). It reads SPIR-V and WGSL and writes SPIR-V, WGSL, HLSL, GLSL and MSL; an `Inspector` pulls reflection data out of a validated program ([tint arch.md L1–L60, L180–L187](https://github.com/google/dawn/blob/43b805ec9edc136b42b55f77ff46f90e73ca2a07/docs/tint/arch.md?plain=1#L180-L187)). Its GLSL writer has desktop and ES standards, defaulting to ES ([version.h](https://github.com/google/dawn/blob/43b805ec9edc136b42b55f77ff46f90e73ca2a07/src/tint/lang/glsl/writer/common/version.h)). Licence BSD-3-Clause ([Dawn README](https://github.com/google/dawn/blob/43b805ec9edc136b42b55f77ff46f90e73ca2a07/README.md?plain=1)). It is built from the Dawn tree, which has a CMake quickstart.
- **WGSL has no preprocessor**, so engines add one. Bevy keys processed shaders by their set of `ShaderDefVal`s ([shader_cache.rs L68–L73](https://github.com/bevyengine/bevy/blob/2614884437512e310eeb196241828162b6507533/crates/bevy_shader/src/shader_cache.rs#L68-L73)) and now composes modules with WESL (`wesl` 0.6 with the `naga-ext` feature) ([bevy_shader Cargo.toml L22](https://github.com/bevyengine/bevy/blob/2614884437512e310eeb196241828162b6507533/crates/bevy_shader/Cargo.toml#L22)).
- In C++ engines read here, Tint appears only as a back end: Filament and bgfx convert SPIR-V to WGSL with it ([bgfx shaderc_wgsl.cpp L31, L778](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/tools/shaderc/shaderc_wgsl.cpp#L31-L778)). None authors in WGSL.

## Route 6: a custom shading language

### bgfx: GLSL-like source, a C preprocessor, per-API back ends

- bgfx's `shaderc` takes a GLSL-like language and "uses an ANSI C pre-processor to transform the GLSL-like language into HLSL". Differences from GLSL: only `float` uniforms, `SAMPLER2D` macros, `mul()` for matrix products, and a `varying.def.sc` file with `$input/$output` instead of `in/out` ([tools.rst L61–L82](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/docs/tools.rst?plain=1#L61-L82)). Profiles select GLSL, HLSL, Metal, PSSL or SPIR-V and set `BGFX_SHADER_LANGUAGE_*` defines; `--define` adds more ([L84–L105, L202–L205](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/docs/tools.rst?plain=1#L84-L205)). "D3D shaders can be only compiled on Windows" ([L229](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/docs/tools.rst?plain=1#L229)).
- Behind it: GLSL goes through glslang (`EShClientOpenGL`), the SPIR-V optimizer for `SPV_ENV_OPENGL_4_5`, and SPIRV-Cross `CompilerGLSL` ([shaderc_glsl.cpp L132, L204, L221–L231](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/tools/shaderc/shaderc_glsl.cpp#L132-L231)). SPIR-V and WGSL go through glslang's **HLSL** front end (`EShSourceHlsl`) ([shaderc_spirv.cpp L499–L506](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/tools/shaderc/shaderc_spirv.cpp#L499-L506); [shaderc_wgsl.cpp L488](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/tools/shaderc/shaderc_wgsl.cpp#L488)), the front end glslang has just deprecated. Metal uses SPIRV-Cross `CompilerMSL` ([shaderc_metal.cpp L717–L736](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/tools/shaderc/shaderc_metal.cpp#L717-L736)); D3D11 loads `D3DCompiler_47.dll` ([shaderc_hlsl.cpp L84](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/tools/shaderc/shaderc_hlsl.cpp#L84)); D3D12 loads DXC (above).
- **Reflection is baked into the binary.** The shader binary format (version 13, with `VSH`/`FSH`/`CSH` magic) carries a uniform table of name, type, count and register index per uniform ([shaderc.cpp L17–L20](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/tools/shaderc/shaderc.cpp#L17-L20); [shaderc_spirv.cpp L355–L382](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/tools/shaderc/shaderc_spirv.cpp#L355-L382)), so the runtime needs no compiler.

### Godot: its own shading language, generated GLSL, then per-driver

- Godot's `ShaderCompiler` turns its shading language into GLSL and returns a `GeneratedCode` with the texture uniforms, the offset of every material uniform and the total uniform size ([shader_compiler.h L64–L80](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/servers/rendering/shader_compiler.h#L64-L80)). Its own compiler is its reflection.
- The GL3 back end compiles that GLSL in the driver with `#define` specializations (route 1).
- The RenderingDevice back ends compile the GLSL to SPIR-V at runtime with glslang, as Vulkan-flavoured GLSL ([modules/glslang/register_types.cpp L67–L78, L110–L134](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/modules/glslang/register_types.cpp#L67-L134); [shader_rd.cpp L1163–L1177](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/servers/rendering/renderer_rd/shader_rd.cpp#L1163-L1177)). Each driver then turns SPIR-V into its own container: D3D12 converts SPIR-V to DXIL through Mesa's NIR ([rendering_shader_container_d3d12.cpp L498–L512](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/drivers/d3d12/rendering_shader_container_d3d12.cpp#L498-L512)); Metal uses SPIRV-Cross `CompilerMSL` and reflects with SPIRV-Reflect ([rendering_shader_container_metal.cpp L39–L44, L664](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/drivers/metal/rendering_shader_container_metal.cpp#L39-L664)).
- `ShaderRD` holds variant defines in groups, compiles groups on a worker pool, and caches compiled variants on disk keyed by SHA-256 ([shader_rd.h L45–L66, L84–L88, L146–L185](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/servers/rendering/renderer_rd/shader_rd.h#L45-L185); [shader_rd.cpp L423–L430](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/servers/rendering/renderer_rd/shader_rd.cpp#L423-L430)).

---

## When: build time or runtime, and hot reload

- **The GL driver always compiles at runtime.** Even with offline translation, GL takes GLSL source (or SPIR-V on 4.6) and the driver finishes the job. Engines cache the driver's result with program binaries: Filament's GL back end tries a blob cache before compiling ([ShaderCompilerService.cpp L388–L413](https://github.com/google/filament/blob/a84309371cca4cbe20efd79e0033ad19849b3c59/filament/backend/src/opengl/ShaderCompilerService.cpp#L388-L413)). Godot's GL3 cache key includes the GL vendor, renderer and version strings ([shader_gles3.cpp L143–L157](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/drivers/gles3/shader_gles3.cpp#L143-L157)), because program binaries are only valid on the driver that produced them.
- **Build time, shipped bytecode.** Filament (`matc`) and bgfx (`shaderc`) compile offline and ship packaged output with reflection inside, so the runtime links no compiler. The CMake shape is one `add_custom_command` per shader (or per variant). `glslc -MD` writes depfiles for `#include`s, and CMake's `DEPFILE` works with Ninja since 3.7, Makefiles since 3.20 and Visual Studio since 3.21 ([CMake add_custom_command](https://cmake.org/cmake/help/latest/command/add_custom_command.html)). Variants multiply build outputs.
- **Runtime, shipped source.** Godot compiles at runtime (glslang in the engine) and caches the results on disk. Every compiler read here has a library API (glslang `TShader`, `libshaderc`, `dxcompiler`, Slang's compilation API, Tint), so runtime compiling is possible with any of them, at the cost of shipping the compiler in the engine binary. DXC and Slang are shared libraries (32 MB and 64 MB release archives respectively); glslang and SPIRV-Cross can be linked statically.
- **Hot reload** needs a compiler reachable at runtime in development builds: either the library linked into a dev configuration, or a file watcher that reruns the offline tool and reloads the output. Neither needs the compiler in shipped builds. On OpenGL-only, hot reload is free: re-read the GLSL and recompile in the driver.

## Reflection and binding models

### The binding models to bridge

Slang's targets chapter is a primary, side-by-side description:

- **OpenGL:** slots, with "distinct kinds of slots for buffers and textures/images", shared by all stages; `binding=2` on a texture and on a buffer is legal in the same program ([09-targets L283–L293](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/09-targets.md?plain=1#L283-L293)).
- **D3D11:** per-stage slots of four register classes: `b` constant buffers, `t` SRVs, `u` UAVs, `s` samplers ([L127–L146](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/09-targets.md?plain=1#L127-L146)).
- **D3D12:** registers plus spaces, mapped onto root parameters, descriptor tables and root constants by a root signature ([L204–L217](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/09-targets.md?plain=1#L204-L217)).
- **Vulkan:** descriptor sets and bindings; all opaque types share one binding index space, so a buffer and a texture at the same set and binding alias ([L246–L259](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/09-targets.md?plain=1#L246-L259)).
- **Metal:** separate binding spaces per resource type (buffer, texture, sampler), plus argument buffers ([L327–L350](https://github.com/shader-slang/slang/blob/f96c0d829e65f3ec463a9382ab286e7db0a5a1cd/docs/user-guide/09-targets.md?plain=1#L327-L350)). SPIRV-Cross maps Vulkan sets to argument buffers or lets the app declare a remapping with `add_msl_resource_binding`, and maps sets to D3D12 register spaces for HLSL SM 5.1+ ([README L412–L427](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/README.md?plain=1#L412-L427)).

The practical consequence: a material's parameters need a layout that each back end can map. Engines read here either author with explicit bindings and let the translator remap (bgfx shifts glslang bindings per class: [shaderc_spirv.cpp L504–L506](https://github.com/bkaradzic/bgfx/blob/21a5cf8fc00888103f97c9ea87ed8a8b4040fb2f/tools/shaderc/shaderc_spirv.cpp#L504-L506)), or let their own compiler assign them (Godot, Slang).

### Where reflection comes from, per route

| Route | Uniform block members and offsets | Resource bindings | Vertex inputs |
|---|---|---|---|
| GLSL only | `glGetProgramResource` (GL 4.3) | same, or names + `glUniform1i` | same |
| SPIR-V (any front end) | SPIRV-Cross `get_shader_resources` / SPIRV-Reflect | decorations `DescriptorSet`, `Binding` | SPIRV-Reflect input variables |
| DXC to DXIL | D3D12 shader reflection (`d3d12shader.h`) | registers and spaces | input signature |
| Slang | Slang `ProgramLayout`, one API for all targets, compile API only | per-target units | entry-point varying parameters |
| Custom (bgfx, Godot) | the engine's own compiler output | same | same |

With SPIR-V on GL 4.6, names may be missing and name queries fail, so reflection must come from the SPIR-V, not the driver (`ARB_gl_spirv` §7.3.1).

## Variants

- **`#define` permutations** are what every engine read here uses for the coarse axes. Filament packs its variants into a bit key (stereo, shadow receiver, skinning, depth, fog, picking, moments, shadow sampler type) and documents 38 used out of 64 ([Variant.h L45–L106](https://github.com/google/filament/blob/a84309371cca4cbe20efd79e0033ad19849b3c59/libs/filabridge/include/private/filament/Variant.h#L45-L106)). Godot GL3 separates "variants" (different data, such as depth prepass against colour pass) from "specializations" (features toggled for performance), both as `#ifdef`s ([shader_gles3.h L76–L80](https://github.com/godotengine/godot/blob/679066724352c12673001d692d41dc2dab9314c0/drivers/gles3/shader_gles3.h#L76-L80)). bgfx passes `--define` to its preprocessor at build time. Lit/unlit and skinned/not map naturally to this.
- **Specialization constants** are a Vulkan and SPIR-V feature (`layout(constant_id)` in Vulkan GLSL, `[[vk::constant_id]]` in DXC HLSL). On GL they become `#define`s via SPIRV-Cross's macro scheme (or `glSpecializeShader` on 4.6). They save front-end work but every value still compiles a separate pipeline in the driver.
- **Slang link-time specialization** reuses precompiled module IR and specializes at link time, which Slang presents as the replacement for preprocessor permutations (route 4).
- **Cost of build-time permutations:** each axis doubles the outputs per target. Filament's filtering of invalid combinations (vertex-only and fragment-only bits) is how it keeps the count down.

## Toolchain cost

| Tool | Licence | Release size (pinned release) | Build integration | Private dependency? |
|---|---|---|---|---|
| glslang | BSD-3 core, plus BSD-2, MIT, Apache-2.0 parts; a GPL-3 text with the Bison exception covers only a Bison skeleton, and "Bison was removed long ago" ([LICENSE.txt L1–L30, L300–L340](https://github.com/KhronosGroup/glslang/blob/5a0d62504b8126220a924613a902e620351d5ebf/LICENSE.txt#L1-L340)) | 13.7 MB Windows / 7.9 MB Linux release archives, tools and libs ([16.6.0](https://github.com/KhronosGroup/glslang/releases/tag/16.6.0)) | CMake; SPIRV-Tools optional (`ENABLE_OPT`), `ENABLE_HLSL=OFF` drops HLSL ([README L229–L264](https://github.com/KhronosGroup/glslang/blob/5a0d62504b8126220a924613a902e620351d5ebf/README.md?plain=1#L229-L264)) | yes, static |
| shaderc | Apache-2.0 ([LICENSE](https://github.com/google/shaderc/blob/ba3e587dbc13d423c713e964ac08e094731a034d/LICENSE)) | builder artifacts only, "not undergone any QA" ([README L24–L33](https://github.com/google/shaderc/blob/ba3e587dbc13d423c713e964ac08e094731a034d/README.md?plain=1#L24-L33)) | CMake, pulls glslang, SPIRV-Tools, SPIRV-Headers | yes |
| SPIRV-Tools | Apache-2.0 (GitHub metadata) | n/a | CMake, needs SPIRV-Headers | yes |
| SPIRV-Cross | Apache-2.0 (GitHub metadata) | source only | CMake `add_subdirectory` recommended for static linking; `SPIRV_CROSS_STATIC/SHARED/CLI` switches; plain C++11 files; vcpkg port ([README L60–L77, L199–L210](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/README.md?plain=1#L60-L210)) | yes, static |
| SPIRV-Reflect | Apache-2.0 (GitHub metadata) | source only | tested on Linux and Windows | yes |
| DXC | NCSA | 32.3 MB Windows zip, 13.3 MB Linux tarball | prebuilt or LLVM-sized source build; loaded as `dxcompiler` DLL/so | yes (dynamic load), not statically |
| Slang | Apache-2.0 with LLVM exception | 63.7 MB Windows zip, 29.7 MB Linux tarball | prebuilt or CMake presets; shared by default | yes (dynamic) |
| naga | MIT OR Apache-2.0 | crates.io | Cargo; no documented C API | only as a build-time CLI or own FFI |
| Tint | BSD-3 (Dawn) | source only | Dawn's CMake | yes |

All of these can stay behind FenrirRenderer's `PRIVATE` link as ADR-0010 requires, provided the public material API names its own parameter types rather than SPIRV-Cross, Slang or D3D reflection types. Build-time-only use keeps them out of the engine's link entirely.

---

## What each route would cost FenrirRenderer

OpenGL first, with D3D, Vulkan or Metal later. These are trade-offs, not a recommendation.

1. **GLSL only, written Vulkan-ready.** Zero dependencies and free hot reload today. If the shaders already use explicit `layout(binding)`/`layout(location)` and keep non-opaque uniforms in UBOs, glslang + SPIRV-Cross can be added later without rewriting them. Loose `uniform` variables would need a rewrite (`GL_KHR_vulkan_glsl`). Reflection comes from the driver now, and must move to SPIR-V reflection later, so the material system should not depend on driver-only facts.
2. **GLSL → SPIR-V → SPIRV-Cross from the start.** The route bgfx, Filament and Godot's RD back ends share. GL gets generated GLSL (or SPIR-V on 4.6); Vulkan gets SPIR-V directly; Metal gets MSL; D3D gets HLSL that must still go through FXC or DXC. Costs: three permissive, static, CMake-friendly libraries; set flattening and combined samplers for GL; reflection from SPIR-V, which is one code path for every back end.
3. **HLSL with DXC.** Best D3D12 story and a mature Vulkan path; GL only through SPIR-V and SPIRV-Cross. Costs: a large prebuilt dependency, Vulkan-only SPIR-V, and register-to-binding shifts. glslang's HLSL is no longer an alternative.
4. **Slang.** One reflection API and modules/generics/link-time specialization, but OpenGL is explicitly "limited", Metal and WGSL experimental, D3D12 needs DXC anyway, and the library is the largest here.
5. **WGSL with naga or Tint.** Fits a WebGPU-shaped engine. For a C++ engine starting on GL, naga needs Rust in the build and Tint lives in the Dawn tree; no C++ engine read here authors in WGSL.
6. **Own language.** Total control of syntax, variants and reflection (Godot's compiler returns uniform offsets directly; bgfx bakes a uniform table), at the price of writing and maintaining a compiler. Both still lean on glslang and SPIRV-Cross for the hard back ends.

## Open questions for the grilling ticket

- Which GL version is the floor: 4.6 (SPIR-V ingestion, `glSpecializeShader`) or 4.3/4.5 (GLSL source with explicit bindings)? The DemoApp currently asks for 4.6.
- Are shaders compiled in the CMake build, in the engine at runtime, or both (runtime only in development builds, for hot reload)?
- Is the material parameter layout derived from reflection, or declared by the engine and checked against reflection?
- Which variant axes does the foundation spec need (lit/unlit, skinned/not), and are they `#define`s, specialization constants, or separate shaders?
