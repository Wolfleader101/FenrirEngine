# What it would take to move FenrirEngine's shaders to Slang

The user asked: "what would it take to move our shader language to Slang?" This file answers that for FenrirEngine as it stands. It covers what Slang offers today, what changes system by system and file by file, the ways to migrate, and whether ADR-0018's reasons for rejecting Slang still hold. It recommends a path but decides nothing: the decision belongs to a superseding ADR.

Read on 2026-11-03, and re-run on 2026-11-06 after the lighting and screen-space reflection work merged into `main`. Two kinds of evidence are kept apart throughout:

- **Tested**: a throwaway prototype, run outside the engine's build (see [Appendix: the prototype](#appendix-the-prototype)). It used `slangc` 2026.17 (`2026.17-25-gc7954ebd1`), which ships in Vulkan SDK 1.4.363.0, and glslang, SPIRV-Tools and SPIRV-Cross from the same SDK. It also ran FenrirShaderCompiler's own `GlRewrite.cpp`, unchanged, built into a small driver. Nothing was loaded into a GL driver, so "valid" below means `spirv-val --target-env opengl4.5` accepts it. No GPU ran anything.
- **Read**: Slang's upstream docs, source and releases, pinned in [What Slang offers now](#1-what-slang-offers-now). FenrirEngine's shaders and compiler are read at `origin/main` @ `cf9d7d93`, which holds the lighting spec (PR #288), the screen-space reflections (ADR-0056, PRs #299 to #301) and the post-effects plan (ADR-0059). The first pass read the lighting branch at `2b9cb165`; the re-run on `main` compiled 67 stages instead of 64 and found the same results, with one new real example of trap 4.

## The short answer

FenrirEngine can move to Slang. The SPIR-V path that GL 4.6 takes survives the move, and most of `FenrirShaderCompiler`'s back half carries over. The cost is in four places:

1. **A new compiler front end.** `StageCompiler` (glslang plus an AST rule checker) and `Reflection` (SPIRV-Reflect) are rewritten on Slang's compilation and reflection API. `Program`'s variant loop, `GlRewrite`, the container and `fenrir_add_shaders` mostly stay.
2. **A large dependency.** Slang is a shared library of roughly 25 MB (`slang-compiler.dll` in the SDK), plus a 6 MB glslang DLL if `-emit-spirv-via-glsl` is used. Today the tool links two small static libraries from submodules. Building Slang from source as a submodule is possible but heavy, so a pinned prebuilt release is the likelier choice.
3. **Porting about 5,100 lines of GLSL.** That is 3,300 in the renderer (28 stages and 16 includes), 1,000 in the demos and about 740 in test fixtures. The includes become modules, and `#pragma fenrir default` becomes an attribute.
4. **Traps found by the prototype.** Each would have shipped wrong pixels or invalid SPIR-V silently:
   - Slang's default matrix layout reads every GLSL matrix transposed.
   - Its GLSL front end drops a vertex stage's `gl_Layer` write.
   - At SPIR-V 1.0 it emits `DrawParameters` without its extension.
   - It strips unused resources, which shifts the GL binding points `GlBindingsOf` computes from a variant's own reflection.

   Every trap has a fix or a workaround, listed below. Each also means a GL golden-image and probe pass is needed for every ported program.

**Recommendation:** no big-bang port, and no Slang compiling the existing GLSL in place. If the user wants Slang, take the gradual path with two front ends:

- Teach `FenrirShaderCompiler` to compile `.slang` stages through Slang beside the GLSL stages it compiles through glslang. Both feed the same `GlRewrite`, container and reflection model.
- Port the shared includes to modules first, then port the programs one at a time. Each ported program is checked against its own goldens.
- Drop glslang when the last GLSL stage is gone.

It is about 13 tickets (see [Migration paths](#3-migration-paths-and-their-risks)).

The strongest reason to do it is a second backend. Slang emits HLSL, Metal and WGSL from the same source, so D3D12, Metal or WebGPU would not need SPIRV-Cross. Its modules and interfaces would also replace the shared includes and `#ifdef` keys with something checked. While FenrirRenderer is GL-only, the benefit is mostly language quality, set against a large dependency and a porting cost. ADR-0018's own revisit conditions do not hold (see [ADR-0018](#4-adr-0018s-revisit-conditions)).

---

## 1. What Slang offers now

Where these sources are pinned:

- Docs, headers and CMake files are read at Slang `master` @ [`7f506f4`](https://github.com/shader-slang/slang/tree/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8) (2026-11-02). Links below abbreviate it as `M`.
- The latest release is still [v2026.20](https://github.com/shader-slang/slang/releases/tag/v2026.20) (2026-10-17), the same release the earlier study ([`shader-languages.md`](#where-the-earlier-research-is)) read. Master is 89 commits past it, and none of the doc changes since touch OpenGL, GLSL, SPIR-V options, reflection, modules or the licence.
- The prototype used the Vulkan SDK's Slang, tag `vulkan-sdk-1.4.363.0`. That is about 2026.17, three releases behind v2026.20.

**SPIR-V output, and GL's path.**

- **No documented OpenGL SPIR-V target or `ARB_gl_spirv` option.** The `-target` list has `spirv` and `glsl` ("GLSL(Vulkan) source code") ([M/docs/command-line-slangc-reference.md L1321–L1360](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/command-line-slangc-reference.md#L1321-L1360)).
- **OpenGL is still second-class.** It "isn't an officially supported target" ([M/docs/target-compatibility.md L4](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/target-compatibility.md#L4)), and the user guide still says Slang has "only limited support for compiling code for OpenGL" ([M/docs/user-guide/09-targets.md L264–L276](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/09-targets.md#L264-L276)).
- **SPIR-V 1.0 to 1.2 output is "still experimental"; 1.3 and later is stable** ([M/docs/user-guide/a2-01-spirv-target-specific.md L10–L13](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-01-spirv-target-specific.md#L10-L13)). GL 4.6's `ARB_gl_spirv` names SPIR-V 1.0. Traps 2 and 3 below are both at 1.0.
- **What does work for GL (tested):**
  - after `GlRewrite`, the output passes `spirv-val --target-env opengl4.5`;
  - `GlRewrite` needs no change.

  It was not loaded into a driver.
- **Combined samplers.** `Sampler2D` and its kin emit `OpTypeSampledImage` with one `[[vk::binding(b, set)]]` ([M/…/a2-01-spirv-target-specific.md L32–L49](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-01-spirv-target-specific.md#L32-L49)), which is what the engine's reflection accepts.
- **Specialization constants.** `[vk::constant_id(N)]`, `[SpecializationConstant]` and GLSL's `layout(constant_id = N)` are all supported ([L216–L237](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-01-spirv-target-specific.md#L216-L237)). So ADR-0051's `fenrirInstanceIndexExcludesBase` carries over (tested).
- **Built-ins differ from GLSL's.** Slang's `SV_InstanceID` is `InstanceIndex - BaseInstance` and `SV_VertexID` is `VertexIndex - BaseVertex` (D3D's meaning). The raw indices are `SV_VulkanInstanceID` and `SV_VulkanVertexID`. `SV_StartInstanceLocation` is `BaseInstance`, and `SV_DrawIndex` is `gl_DrawID` ([L52–L124](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-01-spirv-target-specific.md#L52-L124)). A native `Fenrir.Draw` module must take `SV_VulkanInstanceID` to keep `GlRewrite`'s `InstanceIndex` to `InstanceId` retargeting meaningful, because `SV_InstanceID` would subtract the base twice on GL. GL's `VertexId` and `InstanceId` are not mentioned anywhere.
- **Push constants.** Entry-point `uniform` parameters of plain data become push constants ([L205–L214](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-01-spirv-target-specific.md#L205-L214)). GL has none, so the tool must refuse them, as `RuleChecker` refuses GLSL's.
- **Matrix layout.** `-matrix-layout-row-major` and `-matrix-layout-column-major` are documented ([M/docs/command-line-slangc-reference.md L134–L142](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/command-line-slangc-reference.md#L134-L142)), and the SPIR-V chapter contrasts Slang's default with DXC's ([L355](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-01-spirv-target-specific.md#L355)). What was tested: the default and `-matrix-layout-column-major` both decorate GLSL's matrices `RowMajor`, and `-matrix-layout-row-major` gives glslang's `ColMajor` (trap 1).
- **Other options.** `-fvk-{b,s,t,u}-shift` and `-fvk-bind-globals` follow DXC's meaning, and `-fvk-use-gl-layout` (std430 for raw buffers) and `-fspv-reflect` exist ([L438–L487](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-01-spirv-target-specific.md#L438-L487)). None is needed when every resource carries `[vk::binding]`.

**GLSL output, as the fallback.**

- **Less mature than SPIR-V.** The GLSL target is "less mature than the SPIR-V target" and "not expected to achieve feature parity" ([M/…/a2-04-glsl-target-specific.md L10](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-04-glsl-target-specific.md#L10)).
- **One entry point.** It allows a single entry point, named `main` ([L52–L54](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-04-glsl-target-specific.md#L52-L54)).
- **Layout limits.** Scalar layout "is not expected to be usable for OpenGL" ([L71–L74](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-04-glsl-target-specific.md#L71-L74)), and constant-buffer packing of a 3-vector after a scalar has a known bug ([L88](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-04-glsl-target-specific.md#L88)).
- **What it emits for GL (tested).** Output is Vulkan GLSL (`layout(binding = 0, set = 2)`), so loading it into GL would still need set flattening. It also emits `layout(row_major) uniform;`.
- **The engine's own fallback stays better.** ADR-0018's fallback remains SPIRV-Cross to GLSL from the SPIR-V. That path is independent of the front end and worked on Slang's SPIR-V (tested).

**GLSL as an input.**

- **No chapter and no limitations list.** The README's one line promises "most GLSL intrinsic functions and GLSL's parameter binding syntax" ([M/README.md L39–L43](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/README.md#L39-L43)).
- **Selecting it.** Extensions select GLSL (`.vert`, `.frag`, `.comp` …). `-allow-glsl` is deprecated in favour of `-lang glsl` ([M/docs/command-line-slangc-reference.md L1000–L1003](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/command-line-slangc-reference.md#L1000-L1003)). Through the API it needs `enableGLSL` on the global session ([M/docs/user-guide/08-compiling.md L750–L751](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/08-compiling.md#L750-L751)). Mixing GLSL syntax into Slang source with `import glsl;` is gone from language version 202c ([L221–L225](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/08-compiling.md#L221-L225)).
- **Unknown pragmas.** An unknown `#pragma` warns and is skipped (generated design doc [01-lex-preprocess.md L344–L355](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/generated/design/pipeline/01-lex-preprocess.md#L344-L355); tested: warning E15601).
- **Tested against FenrirEngine's shaders.** 64 of 64 stages compiled after two edits. It also found the traps in §2.3, so GLSL mode is good enough to try but not to trust without a render check.

**Modules, generics, interfaces and specialization.**

- **Modules.** `module`, `import`, `__include` and `implementing` are documented, with access control ([M/docs/user-guide/04-modules-and-access-control.md L9–L107](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/04-modules-and-access-control.md#L9-L107)). `#include` "is still supported". Compared with today's includes, this gives:
  - real namespaces, instead of `Fenrir` name prefixes;
  - private helpers;
  - modules compiled once, instead of re-parsed by every stage of every variant.
- **Generics and interfaces.** Interfaces, generics, associated types, generic value parameters and extensions are documented ([06-interfaces-generics.md](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/06-interfaces-generics.md#L10-L1062)). For FenrirEngine they would replace the lighting include's conventions, such as "fill a `FenrirSurface` and call `FenrirLitColour`", with an `IMaterial` or `ISurface` interface a user's shader implements. That is a checked contract instead of a documented one.
- **Link-time specialization.** Constants use `extern static const` with `export static const`, and types use `extern struct X : I` ([10-link-time-specialization.md L32–L182](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/10-link-time-specialization.md#L32-L182)). They specialize a precompiled module without the preprocessor. They are the documented replacement for `#define` permutations, though the prototype could not link them through `slangc`.
- **Precompiled modules.** `slangc x.slang -o x.slang-module` makes them ([L55–L72](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/10-link-time-specialization.md#L55-L72)).
- **The preprocessor.** `-D` stays ([M/docs/command-line-slangc-reference.md L49–L56](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/command-line-slangc-reference.md#L49-L56)), so today's keys port as they are.

**Explicit layout control.**

- **Bindings and locations.** `[[vk::binding(b, set)]]` is "binding information for Vulkan (and OpenGL)" ([M/docs/user-guide/02-conventional-features.md L691–L699](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/02-conventional-features.md#L691-L699)). `[[vk::location]]` and `[[vk::push_constant]]` are documented beside it ([a2-01 L239–L263](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-01-spirv-target-specific.md#L239-L263)).
- **`ParameterBlock<T>`.** It takes a descriptor set of its own, with its plain data in a uniform buffer at binding 0 ([L194–L202](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/a2-01-spirv-target-specific.md#L194-L202)). That is almost exactly ADR-0024's set 2 with `MaterialParams` at binding 0, so a material could be one `ParameterBlock<Material>`. Whether its automatic numbering fits `GlBindingOf`'s eight-per-kind slots was not tested.
- **Without explicit bindings**, Slang assigns them automatically. The engine's "every resource names its set and binding" rule then becomes a check the tool makes on reflection.

**Reflection, as a replacement for SPIRV-Reflect.**

- **The API.** `ProgramLayout` and `TypeLayoutReflection` give binding ranges, descriptor sets and their ranges, and byte offsets ([M/include/slang.h L2822–L3220](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/include/slang.h#L2822-L3220)). It is one API for every target, and it reports declared parameters whether or not the code uses them (tested via JSON).
- **User attributes** are read back on types, variables and functions with `findUserAttributeByName`, with int, float and string arguments ([L2513–L2549](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/include/slang.h#L2513-L2549), [L3286–L3309](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/include/slang.h#L3286-L3309)). There is no vector argument, so a `float4` default is four floats, as in the prototype.
- **User attributes are marked experimental** ([M/docs/user-guide/03-convenience-features.md L1298–L1321](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/03-convenience-features.md#L1298-L1321)). That is a risk for ADR-0024's defaults.
- **`slangc -reflection-json` exists** ([M/docs/command-line-slangc-reference.md L449–L454](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/command-line-slangc-reference.md#L449-L454); schema v1.1 at [09-reflection.md L1675–L1736](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/09-reflection.md#L1675-L1736)). The same chapter's introduction still says `slangc` cannot give reflection ([L24](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/09-reflection.md#L24)), a stale sentence the earlier study quoted. The JSON includes user attributes (tested), which is undocumented.

**Targets for later backends.**

- **D3D12.** DXIL still goes through DXC: `SLANG_ENABLE_DXIL` fetches or builds it ([M/docs/building.md L298–L302](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/building.md#L298-L302)), and `slangc` runs it as a downstream tool ([08-compiling.md L310–L321](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/08-compiling.md#L310-L321)). HLSL source output needs nothing extra.
- **Metal.** Experimental, for vertex, fragment, compute, task and mesh stages ([M/README.md L107–L121](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/README.md#L107-L121)).
- **WebGPU.** WGSL is experimental, "a work in progress" ([09-targets.md L415–L419](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/user-guide/09-targets.md#L415-L419)).
- **Tested:** the `unlit` port produced HLSL, Metal and WGSL. SPIRV-Cross also accepted Slang's SPIR-V for HLSL and MSL, so ADR-0018's SPIRV-Cross plan stays open whichever front end is used.

**Licence, size, build.**

- **Licence.** Apache-2.0 WITH LLVM-exception ([M/LICENSE](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/LICENSE#L1-L23)), compatible with the engine's other libraries.
- **Size.** v2026.20's Windows x86_64 zip is 63.7 MB. On Linux, the glibc-2.28 tarball is 29.7 MB and the plain zip 87.4 MB ([release](https://github.com/shader-slang/slang/releases/tag/v2026.20)). In the Vulkan SDK the compiler itself is `slang-compiler.dll` at 24.8 MB, plus `slang-glslang.dll` at 6.0 MB and `slang-glsl-module.dll` at 1.4 MB (measured).
- **Building from source:**
  - CMake 3.22 or later (3.26 preferred), C++17, and **Python 3**, for SPIRV-Tools;
  - the git tags must be present, since versioning reads them ([M/docs/building.md L8–L45](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/building.md#L8-L45));
  - CI tests GCC 11 and 13, MSVC 19 and Clang 17 ([L558–L570](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/building.md#L558-L570));
  - code generators run at build time ([L457–L477](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/building.md#L457-L477)).

  The rest of the engine needs none of these: today's `libs/CMakeLists.txt` turns glslang's binaries off precisely to avoid Python.
- **Static linking.** `SLANG_LIB_TYPE` defaults to `SHARED`. `STATIC` is supported and now exports CMake targets ([M/CMakeLists.txt L406–L416, L940–L976](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/CMakeLists.txt#L406-L416)).
- **Downloads during the build.** By default the build downloads DXC, slang-llvm, Dawn and Tint unless switched off (`SLANG_ENABLE_DXIL`, `SLANG_SLANG_LLVM_FLAVOR=DISABLE`, `SLANG_EXCLUDE_DAWN`, `SLANG_EXCLUDE_TINT`, `SLANG_ENABLE_SLANG_RHI`, [building.md L293–L416](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/docs/building.md#L293-L416)).
- **As a submodule.** Using it as a submodule through `add_subdirectory` is undocumented, and its `CMakeLists.txt` has no `PROJECT_IS_TOP_LEVEL` guard. A submodule like glslang's is possible but unsupported, and it would bring Python and a long first build to every clone.
- **Prebuilt options.** vcpkg's `shader-slang` port is at 2026.20 and repackages the release binaries, dynamic only ([port](https://github.com/microsoft/vcpkg/blob/c653b77e80d1b61e5f5bd2eabf15bc6ec3bfe7ad/ports/shader-slang/portfile.cmake#L1-L22)). The Vulkan SDK has shipped Slang since 1.3.296.0 ([M/README.md L52–L53](https://github.com/shader-slang/slang/blob/7f506f4c9ea8b6d37f43e8d3e3eadf074d61aed8/README.md#L52-L53)).

**Governance.** Khronos hosts Slang under its Slang Initiative (announced 2024-11-21), and Valve, id Software and others are named as users ([Khronos press release](https://www.khronos.org/news/press/khronos-group-launches-slang-initiative-hosting-open-source-compiler-contributed-by-nvidia)). Khronos's Vulkan-Samples accept Slang shaders ([README L23–L33](https://github.com/KhronosGroup/Vulkan-Samples/blob/56a72bd1c43268781a75d73a520e4458b6cac766/shaders/README.adoc#L23-L33)), and the Vulkan Tutorial compiles with `slangc` ([source](https://github.com/KhronosGroup/Vulkan-Tutorial/blob/54b886e3d9126c3bb61badfcdec2c001ead055e2/en/03_Drawing_a_triangle/02_Graphics_pipeline_basics/01_Shader_modules.adoc#L8-L35)). It is not going away, but none of these users is on OpenGL.

### Where the earlier research is

ADR-0018 cites `research/shader-languages`: `.scratch/fenrir-renderer/research/shader-languages.md`, added in commit `103a2000` and since removed from the tree. Read it with `git show 103a2000:.scratch/fenrir-renderer/research/shader-languages.md`. Its Slang section (route 4) still stands, with two corrections:

- `slangc` can emit reflection, through `-reflection-json`.
- The "29.7 MB Linux" figure is the glibc-2.28 tarball, not the plain Linux zip (87.4 MB).

## 2. What changes in FenrirEngine

### 2.1 The shaders today

Counted at `main` @ `cf9d7d93`:

| Group | Files | Lines | Notes |
|---|---|---|---|
| Built-in stages, `packages/FenrirRenderer/Shaders/Builtin/` | 28 | 1,946 | 24 vertex and fragment stages, plus `skinning.comp` (93) and `light_clusters.comp` (136); biggest are `gtao.frag` (299), `reflection_trace.frag` (223), `output.frag` (144), `lit.frag` (140). ADR-0056 added `hi_z.frag` (47), `reflection_trace.frag` and `scene_history.frag` (48) |
| Shared includes, `Shaders/include/Fenrir/` | 16 | 1,370 | `Lighting.glsl` 424, `Shadows.glsl` 227, `Draw.glsl` 128, `Frame.glsl` 116, `SkinMaths.glsl` 98, `View.glsl` 71, `Skinning.glsl` 59, `LightRecord.glsl` 43, `Brdf.glsl` 43, `CubeFaces.glsl` 38, `Normals.glsl` 32, `Clusters.glsl` 30, `Vertex.glsl` 22, `2D.glsl` 17, `SphericalHarmonics.glsl` 15, `Constants.glsl` 7 |
| Demos | 17 | 1,016 | LightingDemo 906 (`facade.frag` 327), RendererDemo 82, AnimationDemo 28 |
| Renderer test fixtures | 11 | 279 | `Tests/fixtures/`, `Tests/OpenGL/fixtures/` |
| FenrirAssets test fixtures | 2 | 37 | `Tests/fixtures/shaders/` |
| FenrirShaderCompiler test fixtures | 27 | 426 | 15 of them in `errors/`, which must fail to build |

There are 24 `fenrir_add_shaders` calls for built-in programs (`packages/FenrirRenderer/CMakeLists.txt`). The variant keys in use are `ALPHA_MASK`, `SKINNED`, `POINT_SHADOW`, `CUBEMAP`, `MSAA`, `DENOISE`, `OCCLUSION`, `REFLECTIONS`, `COPY`, `DOWNSAMPLE`, `INPUT_SRGB`, `PREMULTIPLY` and `GAMMA`, at most three per program, so at most 8 variants. `builtin:lit`, `builtin:unlit`, `builtin:output` and `builtin:2d` have 8 each. The compiler tool, the container, `fenrir_add_shaders` and the golden-image comparison are unchanged between `2b9cb165` and `main`.

C# never reaches the compiler. `Fenrir.Assets.Shaders` (`packages/FenrirAssets/Managed/Fenrir.Assets/Shaders.cs` and its generated part) reads a shader's material parameters from the container through FenrirAssets. Users' shaders reach the compiler only through `fenrir_add_shaders` in their CMake (`cmake/FenrirShaders.cmake`). The C# side therefore sees no change as long as the container's reflection model (parameters, types, defaults) stays the same.

### 2.2 System by system

**`FenrirShaderCompiler` (1,832 lines of source, 653 of tests).**

| Part | Today | With Slang |
|---|---|---|
| `StageCompiler.cpp` (317) | glslang `TShader` with a `#define` preamble, the include callback, Vulkan 1.0 / SPIR-V 1.0 target. Then a `RuleChecker` walks glslang's AST: every resource has `set` and `binding`, no push constants, no `gl_InstanceIndex` outside `Fenrir/Draw.glsl`. | Rewritten on Slang's API (`IGlobalSession`, `ISession` with search paths and macros, `loadModule`, `link`, `getTargetCode`), with these target options: SPIR-V, profile `spirv_1_0` (or higher, see below) and **row-major matrix layout** (trap 1). There is no AST to walk, so the rules move: <br>- An explicit binding becomes a check on reflection, or a warning made an error (see below). <br>- Push constants become a reflection check. <br>- The `gl_InstanceIndex` ban can become structural: Slang system values are entry-point parameters, so a `Fenrir.Draw` module can be the only code that takes `SV_InstanceID` and hands out `CurrentDraw()`. <br>The depfile can come from `ISession`'s loaded-file list, or `slangc -depfile`. |
| `GlRewrite.cpp` (120) | Zeroes sets, flattens bindings, retargets `VertexIndex`/`InstanceIndex`, flips the spec constant `61440`. | **Unchanged.** Tested on Slang's SPIR-V for `builtin:lit` (`ALPHA_MASK`, `SKINNED`), `builtin:output` (`MSAA`, `OCCLUSION`), `builtin:2d` (`GAMMA`) and a native-Slang `unlit`:<br>- every binding point matched glslang's;<br>- `InstanceIndex` and `VertexIndex` were retargeted;<br>- the spec constant (Slang keeps `constant_id` as `SpecId 61440`) flipped to true;<br>- the output passed `spirv-val --target-env opengl4.5`.<br>Its input changes, though (trap 4). |
| `Reflection.cpp` (192) | SPIRV-Reflect on the SPIR-V. A block is named by its type name (`MaterialParams`, `Target2D`). It also reads the runtime-array record size, member offsets and types. | Rewritten on Slang's `ProgramLayout`, or kept on SPIRV-Reflect with two fixes:<br>- Slang names block types differently: `SLANG_ParameterGroup_MaterialParams_std140` for GLSL input, `MaterialParams_std140` for a `ConstantBuffer<MaterialParams>`. The `MaterialBlockName` and `Target2DBlockName` matches in `Program.cpp` would miss both.<br>- Slang strips unused resources from the SPIR-V (trap 4), so reflection must come from Slang's own layout. That layout lists every declared parameter (tested with `-reflection-json`).<br>The better route is Slang's reflection: one API for every later target, and it carries user attributes (the defaults). |
| `Pragmas.cpp` (266) | Finds `#pragma fenrir default(...)` in the preprocessed source, so each variant keeps only the pragmas its `#define`s keep. Then it re-parses each from its source line. | **Replaced by user attributes**, for example `[FenrirDefault(1, 1, 1, 1)]` on a `MaterialParams` field and `[FenrirDefaultTexture("white")]` on a texture. They come back through reflection with typed arguments (tested: `-reflection-json` shows `"userAttribs": [{"name": "FenrirDefault", "arguments": [1.0, ...]}]`). That removes the line re-parsing that the preprocessor's token-joining forced. The checks in `Program.cpp`'s `ApplyDefaults` stay: unknown member, value count, stages disagreeing, non-parameter types. Slang ignores an unknown `#pragma` with warning E15601 (tested), so GLSL-mode stages could keep the pragma parser while they last. |
| `Program.cpp` (425) | Variant loop over every combination of `#define` keys; merge stages; material set; defaults; default block; GL copy. | Mostly unchanged. Variants stay `#define` keys passed as preprocessor macros: `-D` and `#ifdef` work in `.slang` files (tested on a native port). The block-name matches and the binding merge move to Slang reflection. |
| `Arguments`, `main`, `Diagnostics` (~330) | CLI and errors. | Small changes: diagnostics come from Slang's `IBlob`. |
| Tests (653 lines, 23 cases, 15 must-fail fixtures) | Pin glslang's messages, for example `'location' : SPIR-V requires location`. | Every expected message in `Tests/CMakeLists.txt` that quotes glslang changes. The engine's own messages (pragmas, nine textures, `gl_InstanceIndex`) stay if the checks are ported. |

**Includes become modules.** `#include "Fenrir/Lighting.glsl"` becomes `import Fenrir.Lighting;`. Each include's `#ifndef` guard goes, and so does `GL_GOOGLE_include_directive`. `fenrir_add_shaders`' `--include` directories become Slang search paths. A module can be precompiled to `.slang-module` (tested: `Fenrir/View.slang` gives a 35 KB module), which would let the engine ship its modules prebuilt for users' shaders. Two include habits need a design:

- **Includes that change under a key.** `Draw.glsl` changes under `POINT_SHADOW` and `Skinning.glsl` only exists under `SKINNED`. Modules are compiled once, not per includer's `#define`, so those keys either stay preprocessor macros visible to the module (Slang applies session-wide `-D` macros to every module it compiles) or become link-time constants or generic parameters.
- **Set declarations.** `Frame.glsl`, `View.glsl` and `Draw.glsl` hold each set's declarations. They become modules exporting `ConstantBuffer<…>` and `StructuredBuffer<…>` globals with `[vk::binding(b, set)]` (tested on `Fenrir/View.slang`).

**Reflection and ADR-0024's material set.** The model does not change: set 2 is reflected, and sets 0, 1 and 3 are declared in C++ and checked in debug builds. What changes is where the facts come from: Slang's `ProgramLayout`, in bytes and in descriptor slots. Two differences matter:

- Slang does not keep unused resources in a stage's SPIR-V.
- Its std140 layout of `MaterialParams` must match glslang's offsets, or every saved default block image changes. The tested `unlit` port's `ConstantBuffer<MaterialParams>` came out as a `_std140` block, at offsets 0 and 16 for `baseColour` and `alphaCutoff`, as std140 gives. Every built-in's block should be compared with glslang's offsets before its port lands.

The container's `ShaderBlockMember` and `ShaderParameter` stay as they are.

**The shader container.** No format change is needed. A stage keeps its GL SPIR-V copy and its unrewritten SPIR-V (`ShaderContainer.hpp`, lines 141 to 151). The doc comment on `spirv` ("as glslang made it") changes wording, not layout, so `ShaderContainerVersion` can stay 1. A version bump comes only when a second backend adds per-target outputs, and that is true with or without Slang.

**CMake.** `fenrir_add_shaders` keeps its signature. `VERTEX`, `FRAGMENT` and `COMPUTE` accept `.slang` files beside `.vert`, `.frag` and `.comp`. Optionally one `.slang` file with several `[shader("…")]` entry points can serve both stages, which Slang allows. `INCLUDE_DIRECTORIES` doubles as module search paths. `packages/FenrirRenderer/Tools/ShaderCompiler/libs/CMakeLists.txt` adds Slang beside glslang during the move, then drops glslang. ADR-0010's rule holds either way, since only the build-time tool links it.

**Goldens and probes.** They are **not** expected to stay bit-identical, and they do not need to:

- ADR-0049's golden images compare by CIEDE2000 under a tolerance (`packages/FenrirRenderer/Tests/GoldenImages.cpp`).
- The GL tests' pixel probes compare with tolerances.

Slang's SPIR-V differs from glslang's in instruction order, in which locals it keeps, and possibly in floating-point contraction. So the expectation is "within tolerance", and each ported program must pass its goldens and GL tests before the next is ported. Trap 1 shows why that check is not optional: with Slang's default matrix layout every golden would fail outright.

### 2.3 What the prototype found: the traps

| # | Trap | Evidence (tested) | Fix |
|---|---|---|---|
| 1 | **Matrices read transposed.** For GLSL input, Slang's default (`-matrix-layout-column-major`) decorates every matrix member `RowMajor`. glslang decorates them `ColMajor`. The multiply is still `OpMatrixTimesVector`, so `view.viewProjection * world` reads the C++ column-major bytes transposed. SPIRV-Cross decompiles Slang's version as `spvWorkaroundRowMajor(view.viewProjection) * world`. | `mat.vert` repro; `mesh.vert` (9 matrix members) | `-matrix-layout-row-major` (Slang's names are swapped relative to GLSL's) gives `ColMajor` and the same code as glslang. Tested on `mesh.vert` and the native `unlit` port. The flags are documented, but which SPIR-V decoration each gives is not (§1). |
| 2 | **`gl_Layer` dropped.** A GLSL vertex stage's `gl_Layer = …` (`Draw.glsl`'s `POINT_SHADOW` path, ADR-0050) produces no `Layer` builtin and no store, with no diagnostic, at every SPIR-V version and in Slang's GLSL output too. | `layer.vert` repro; `mesh.vert -DPOINT_SHADOW` | Write it the Slang way: an `SV_RenderTargetArrayIndex` output. At SPIR-V 1.5 that gives `ShaderLayer` and a valid `Layer` store. At SPIR-V 1.0 it emits `Capability Geometry` without `ShaderViewportIndexLayerEXT`, and `spirv-val` rejects it. So either the GL path takes SPIR-V newer than 1.0 (untested on drivers), or the compiler patches the capability and extension as `GlRewrite` already patches builtins. |
| 3 | **`DrawParameters` without its extension.** At `-profile spirv_1_0`, a stage reading `gl_BaseInstance` gets `OpCapability DrawParameters` but no `OpExtension "SPV_KHR_shader_draw_parameters"`. `spirv-val` rejects it for OpenGL 4.5 and for Vulkan 1.0. glslang emits both. | `mesh.vert` (every mesh program) | Same choice as trap 2: SPIR-V 1.3+ (where it is core), or have `GlRewrite` add the extension. A driver may not care, but the tool should not ship invalid SPIR-V. |
| 4 | **Unused resources stripped, so binding points shift.** Slang drops resources a stage never reads; glslang keeps every declared one. `GlBindingsOf` numbers a binding by "how many bindings of its kind in its set have a lower binding number in this variant". A variant that declares set 0's textures at bindings 1 and 2 but reads only binding 2 would put it at GL point 0, while the renderer binds it at point 1 from its declared layout: the wrong texture, silently. `-preserve-params` did not keep them in GLSL mode. It happens in real built-ins on `main`: `mesh.vert` declares set 1's five textures and buffers through `View.glsl` and reads none, and `scene_history.frag`'s variant without `DOWNSAMPLE` declares its `HistoryData` block and never reads it. Both were stripped. | `gap.frag` repro, run through `GlRewrite`; `mesh.vert` and `scene_history.frag` on `main` | Take the variant's bindings from Slang's reflection, which lists every declared parameter (tested), not from the SPIR-V. Or change the flattening formula so it does not depend on which bindings a variant uses. |
| 5 | **Two GLSL constructs Slang's GLSL mode refuses.** `invariant gl_Position;` (`Vertex.glsl`, which ADR-0049's equal depth test relies on) and `mat4(mat3)` (`Draw.glsl`'s instance normal and cube-face turn). With both edited, all 64 non-error GLSL stages compiled. | all 64 stages | `mat4(mat3)` is spelled out by hand. `invariant` has no GLSL-mode spelling. Native Slang has no `invariant` keyword either (`invariant float4 pos : SV_Position` is a parse error; tested). The upstream docs read do not say how a Slang vertex stage marks `SV_Position` invariant. It needs an answer before the depth prepass can move: a `precise` qualifier, SPIR-V inline assembly, or the tool adding `Invariant` to the `Position` builtin, as `GlRewrite` already edits decorations. |
| 6 | **Missing capability on cube arrays.** For `samplerCubeArrayShadow` (`Frame.glsl`'s point shadow cubes), Slang emits an arrayed `Cube` image without `OpCapability SampledCubeArray`. glslang declares it. `spirv-val` accepts both. | `lit.frag`, `cubearr.frag` | Watch for it on drivers. Strict Vulkan validation would want the capability. |

Slang's SPIR-V is also smaller, after its own dead-code elimination: `mesh.vert` came out at 7.3 KB against glslang's 9.6 KB. It also does not emit `ClipDistance`, `CullDistance` or `PointSize` unless they are used.

### 2.4 Docs and ADRs to change

- **ADR-0018:** superseded. Its decisions on language, rules, includes and libraries change; its decisions on SPIR-V, building at build time, the container and `fenrir_add_shaders` survive.
- **ADR-0024:** amended. `#pragma fenrir default` becomes an attribute, and the reflection source changes.
- **ADR-0048:** amended. "One shared lighting include" becomes a module.
- **ADR-0051:** amended. `Fenrir/Draw.glsl`, the spec constant `fenrirInstanceIndexExcludesBase` and the `gl_InstanceIndex` ban keep their meaning but are restated for a module.
- **ADR-0049 and ADR-0050:** checked, not changed. The invariant position and the `gl_Layer` write must hold after the port.
- **ADR-0056:** amended. The reflection blend lives in `Fenrir/Lighting.glsl`, and the view set's `fenrirReflectionHits` and `fenrirSceneHistory` are declared in `View.glsl`; both are restated for modules. Its three new programs port like the other fullscreen passes.
- **ADR-0059:** checked. It plans lens effects and grading as more fullscreen passes, and names no GLSL file yet, so whichever language lands first for them is the one they are written in. If the port starts before lighting tickets 25 to 28, those tickets should write Slang.
- **`packages/FenrirRenderer/README.md`:** "User shaders", which names `Fenrir/Vertex.glsl` and `invariant gl_Position`, and "Materials and built-in shaders".
- **`cmake/FenrirShaders.cmake`'s header comment.**
- **`GLOSSARY.md`:** "FenrirShaderCompiler" ("compiles GLSL to SPIR-V") and "Shader variant" ("`#define` keys").
- **Every shader's header comment that names a `.glsl` include.**

## 3. Migration paths and their risks

### A. Big bang

Replace glslang with Slang and port all 5,100 lines in one stack.

- **For:** one toolchain at every point, and no period with two front ends.
- **Against:** every program changes at once. A wrong-pixel trap (1, 2 or 4) shows up as dozens of golden failures with no way to bisect by program. It also blocks every other renderer change while it lands, after a lighting spec that just added most of the shader code.
- **Not recommended.**

### B. Slang compiles the existing GLSL in place

Swap the compiler first, keep the GLSL, then convert files to Slang syntax at leisure. Slang's GLSL mode (`-allow-glsl`) did compile all 64 stages after two edits.

- **For:** the tool changes once, and the shader sources barely move.
- **Against:** the prototype found this is exactly where Slang is weakest:
  - the dropped `gl_Layer` (trap 2) is in the GLSL front end;
  - the `invariant` gap and the `mat4(mat3)` refusal are GLSL-mode gaps;
  - the transposed matrices (trap 1) only bite GLSL-style code.

  It changes the compiler of every program at once, as A does, and keeps the language the move is meant to leave.
- **Not recommended.**

### C. Two front ends, port program by program (recommended)

The tool compiles `.vert`, `.frag` and `.comp` through glslang, as now, and `.slang` through Slang. Both produce SPIR-V 1.0 (or whatever the GL path settles on) with the same rules. Both feed the same reflection model, `GlRewrite` and container. A program may even mix a GLSL vertex stage with a Slang fragment stage, since interfaces meet at SPIR-V locations. Each program moves when it is ready and is checked against its own goldens. The traps are met one program at a time:

- trap 1 on the first program;
- trap 4 on the first that reads part of a set;
- trap 2 at the point-shadow variant.

Rough tickets, each a stackable PR:

1. **Superseding ADR:** the decisions in §4.
2. **Fetch Slang:** a pinned prebuilt release at configure time, or a submodule, per the ADR. Wire it into `libs/CMakeLists.txt` for the tool only, on MSVC, clang-cl and Linux CI.
3. **Slang front end in `FenrirShaderCompiler`:** `.slang` stages through the Slang API with row-major matrices, the chosen SPIR-V version, `-D` keys and search paths; the depfile; diagnostics.
4. **Reflection from Slang's layout:** for `.slang` stages, the bindings, `MaterialParams` members and runtime-array record size, with the block-name matching made source-neutral. Must-fail fixtures for the binding and push-constant rules.
5. **Defaults as attributes:** `FenrirDefault` and `FenrirDefaultTexture`, read from reflection, through `ApplyDefaults`' existing checks, with the pragma error fixtures mirrored as `.slang` ones.
6. **GL fix-ups:** either `GlRewrite` adds `SPV_KHR_shader_draw_parameters` and `ShaderViewportIndexLayerEXT` with its extension where Slang leaves them out, or the GL path moves past SPIR-V 1.0, after a driver test on NVIDIA, AMD, Intel and Mesa.
7. **Modules for sets 0, 1 and 3:** `Fenrir.Frame`, `Fenrir.View` and `Fenrir.Draw` (with `CurrentDraw()` as the only way to the instance index, taken as `SV_VulkanInstanceID`, never `SV_InstanceID`, which already subtracts the base), with `Fenrir.Vertex`, `Fenrir.Normals` and `Fenrir.CubeFaces`.
8. **Modules for lighting:** `Fenrir.Brdf`, `Fenrir.LightRecord`, `Fenrir.Clusters`, `Fenrir.SphericalHarmonics`, `Fenrir.Shadows` and `Fenrir.Lighting` (`FenrirSurface` could become a struct with methods; the BRDF pieces could become an interface).
9. **Port the fullscreen and post programs:** `fullscreen.vert`, background, panorama, sky filtering, BRDF LUT, bloom, GTAO, Hi-Z, reflection trace, scene history and output (about 1,200 lines).
10. **Port the mesh programs:** `mesh.vert`, unlit, lit, fallback, prepass and shadow, with `POINT_SHADOW` and `SKINNED`. This is where traps 2 and 5 (invariant) land.
11. **Port compute, debug and 2D:** skinning, light clusters, debug lines, triangles and text, and 2D.
12. **Port the demos and test fixtures:** LightingDemo, RendererDemo, AnimationDemo and the FenrirAssets and renderer fixtures.
13. **Remove glslang:** the GLSL path, SPIRV-Reflect if the reflection fully moved, the GLSL must-fail fixtures, and the docs in §2.4.

That is 13 tickets, the last few large. Tickets 7 and 8 can run beside 3 to 6 once the module layout is agreed. Tickets 9 to 12 can run in parallel with each other when their files are disjoint.

- **Risks:**
  - two compilers in the tool for the length of the move;
  - module boundaries designed under time pressure;
  - user shaders written against the GLSL includes need a deprecation window, since `fenrir_add_shaders` is public;
  - per-program goldens must exist for every ported program, and the 2D, debug and compute ones are thinner today.

### D. Slang for new code only

Add the Slang front end (tickets 2 to 6) and write only new programs in Slang. Keep the GLSL includes as they are.

- **For:** cheapest, and new features get modules and generics.
- **Against:** every shared concept (sets 0, 1 and 3, `FenrirSurface`, the lights) then exists twice, once as a `.glsl` include and once as a module, and the two drift. Only worth it as a time-boxed first step of C.

## 4. ADR-0018's revisit conditions

ADR-0018 named two triggers:

- **"If GL drivers' SPIR-V path proves unreliable in practice."** Nothing in the lighting spec reported that: every built-in, including compute, runs through `glShaderBinary` and `glSpecializeShader`. Even if it did, the ADR's answer is SPIRV-Cross to GLSL for GL, not Slang. **Does not hold.**
- **"Variants outgrow building every combination offline."** At most three keys and 8 variants a program today. **Does not hold.**

Its three reasons for rejecting Slang:

- **"Its GL support is documented as limited."** It still is, word for word, and OpenGL is "not an officially supported target". SPIR-V 1.0, the version GL takes, is now documented as experimental (§1). The prototype shows the GL SPIR-V path works after `GlRewrite`, but only with the fixes for traps 1 to 4. **Still holds**, though it is weaker than "limited" suggests.
- **"D3D12 still goes through DXC."** Still true. It is no worse than SPIRV-Cross's HLSL, which also needs DXC (or FXC) for DXIL. **Holds, but it is not specific to Slang.**
- **"A large shared library."** Still true: about 25 MB for the compiler. A static build exists but is undocumented as a submodule. Since only the build-time tool links it, it never reaches the engine binary. The cost is in the build and on CI. **Holds.**

None of the three reasons has gone away. What has changed is what is known about the cost: GL's SPIR-V path is shown to be workable, the traps are found and each has a fix, and reflection and attributes cover ADR-0024. So adopting Slang now is a choice the user can make on the language's merits, not because ADR-0018's conditions forced it.

What a superseding ADR would have to decide:

1. **Why now.** A second backend, the language itself (modules, generics, interfaces), or both, since the ADR's own triggers have not fired.
2. **How Slang is fetched:** a submodule built from source (heavy, see §1) or a pinned prebuilt release at configure time. ADR-0052 already downloads large demo assets at configure time. Also decide on Windows, Linux and the CI images.
3. **The SPIR-V version for GL:** 1.0 with `GlRewrite` patching capabilities, or a newer version after a driver test. `ARB_gl_spirv` names SPIR-V 1.0.
4. **Where reflection comes from:** Slang's layout for everything, with SPIRV-Reflect dropped, and how the flattening formula copes with stripped resources.
5. **Defaults syntax:** the attribute names and arguments, replacing ADR-0024's pragma.
6. **Variant model:** keep `#define` keys (simplest, and the container is unchanged), or move some to link-time constants or specialization constants. The ADR keeps every combination offline unless it says otherwise.
7. **The rules restated:** explicit bindings, no push constants, the instance-index ban, invariant position, the matrix layout flag, and how each is enforced without glslang's AST.
8. **Module layout and naming,** and whether the engine ships precompiled `.slang-module` files for users.
9. **User shaders:** whether GLSL user shaders stay accepted (two front ends forever) or are deprecated by a date.
10. **Future backends:** whether D3D12, Metal and WebGPU get Slang's own targets or SPIRV-Cross from Slang's SPIR-V. The container then carries per-target outputs, with or without Slang.

---

## Appendix: the prototype

Everything ran in the session's scratchpad, outside the repository and its build. Inputs were `git archive` of `packages/FenrirRenderer`, `packages/FenrirAssets`, `cmake` and the demos, first at `origin/fenrir-renderer-lighting` (`2b9cb165`), then at `origin/main` (`cf9d7d93`); the numbers below are from the `main` run, which repeated every step. Tools: Vulkan SDK 1.4.363.0's `slangc` 2026.17, `glslangValidator`, `spirv-val`, `spirv-dis` and `spirv-cross`.

1. **Every GLSL stage through Slang's GLSL mode.** All 67 stages on `main`, the 15 `errors/` fixtures left out, were compiled with:

   ```
   slangc <stage> -lang glsl -stage <s> -entry main -target spirv -profile spirv_1_0 -matrix-layout-row-major -I <Shaders/include> -o out.spv
   ```

   54 compiled. The other 13, every vertex stage that includes `Vertex.glsl`, failed on `invariant gl_Position;` (`Vertex.glsl:11`), and `mesh.vert` also on `mat4(copy.normal)` (`Draw.glsl:118`). With those two lines edited in a copy, all 67 compiled. `mesh.vert -DPOINT_SHADOW` also needed `Draw.glsl`'s `mat4(turn)` spelled out. ADR-0056's `hi_z.frag`, `reflection_trace.frag` and `scene_history.frag` compiled as written.
2. **SPIR-V version.** Slang's default output is SPIR-V 1.5, which `spirv-val --target-env vulkan1.0` rejects. `-profile spirv_1_0` gives 1.0 (with "profile implicitly upgraded" warnings for capabilities such as `spvImageQuery`).
3. **Decorations compared with glslang** (a small `spirv-dis` parser):
   - sets, bindings, locations, `SpecId 61440`, combined image samplers, `OriginUpperLeft`, `VertexIndex`, `InstanceIndex` and `BaseInstance` match for `mesh.vert` and `lit.frag`;
   - block type names differ (`SLANG_ParameterGroup_<name>_std140`);
   - matrix majorness differs (trap 1);
   - `Layer` is missing (trap 2);
   - unused resources are missing (trap 4).
4. **`GlRewrite` on Slang's SPIR-V.** FenrirShaderCompiler's `GlRewrite.cpp` and `FenrirAssets/ShaderGlBindings.hpp`, unchanged, were built with clang++ into a 40-line driver. The driver reads SPIR-V and a binding list, calls `GlBindingsOf` and `RewriteForGl`, and writes the GL copy. For `builtin:lit` (`ALPHA_MASK`, `SKINNED`), `builtin:output` (`MSAA`, `OCCLUSION`; on `main` also `MSAA`, `REFLECTIONS`), `builtin:2d` (`GAMMA`), and on `main` `builtin:hi-z`, `builtin:reflection-trace` and `builtin:scene-history` (each with `MSAA`), compiled by both compilers:
   - every GL binding point present in both was identical (`lit.frag` on `main`: 23 of 23);
   - the builtins and the spec constant were rewritten the same way;
   - Slang's copies passed `spirv-val --target-env opengl4.5`, except the vertex stage reading `gl_BaseInstance` (trap 3).
5. **Native Slang.** A `Fenrir/View.slang` module (`[vk::binding]` globals) and an `unlit.slang` port using it:
   - **Defaults:** `[FenrirDefault(...)]` and `[FenrirDefaultTexture("white")]` user attributes appeared in `-reflection-json` output with their arguments.
   - **Variant key:** `ALPHA_MASK` as `-D` with `#ifdef` produced `OpKill` only when set.
   - **GL copy:** passed `spirv-val --target-env opengl4.5`.
   - **Other targets:** `-target glsl`, `hlsl`, `metal` and `wgsl` all produced output, and SPIRV-Cross accepted the SPIR-V for HLSL SM 5.0 and MSL.
   - **Module:** precompiled to a 35 KB `.slang-module`.
   - **Not working:** link-time specialization of the same key (`extern static const bool`, with an `export` in a second module) did not link through `slangc` in a few attempts ("conflicting declaration", or "undefined identifier" from precompiled modules). Upstream documents it through the compilation API (§1), which the prototype did not call.
6. **Repros.** `layer.vert` and `layerfn.vert` showed no `Layer`. `layer.slang` with `SV_RenderTargetArrayIndex` gave `Layer` at 1.5 and invalid SPIR-V at 1.0. `mat.vert` showed the `RowMajor` versus `ColMajor` difference. `gap.frag` showed `first` stripped even with `-preserve-params`, and `second` flattened to GL point 0. `cubearr.frag` showed the missing `SampledCubeArray`. `invariant.slang` was a parse error.

Not tested: loading any of it into a GL driver, rendering, the goldens, the Slang C++ API, building Slang from source, and Slang releases newer than 2026.17.
