# 06: FenrirShaderCompiler and the shader container

**What to build:** GLSL 460 programs are compiled at build time into versioned shader containers holding GL-ready SPIR-V, reflection and checked parameter defaults, and FenrirAssets reads a container's parameter table (spec: "Shaders and `FenrirShaderCompiler`", "FenrirAssets" (`ShaderData`, `GetShaderParameters`), "Build" (glslang and SPIRV-Reflect rows), "Testing Decisions" seam 5; ADR-0018, ticket 41). No shader draws yet; ticket 11 writes the first built-ins.

- **The tool:** an executable under `packages/FenrirRenderer/Tools/ShaderCompiler/`, linking glslang and SPIRV-Reflect (pinned submodules under its `libs/`, static; glslang with no HLSL front end, optimizer, binaries, tests or install; SPIRV-Reflect's static library only), never linked into the engine. Built under `FENRIR_ENABLE_RENDERER`.
- **`fenrir_add_shaders(<target> ...)`:** runs the tool through `add_custom_command` with depfiles, for the engine's shaders and users' alike, naming each program's variant keys.
- **Per program:** resolve `#include` (`GL_GOOGLE_include_directive`) against the engine's shared include directory; build every combination of the named `#define` keys; compile each stage as Vulkan GLSL to SPIR-V 1.0; write the GL copy (sets zeroed, bindings flattened per resource kind as `set × 4 + binding`, `VertexIndex` retargeted to `VertexId`); reflect and check defaults; write one container.
- **Rules enforced:** explicit `layout(set, binding)` and `layout(location)`; uniforms in blocks; `gl_InstanceIndex` refused with a clear error.
- **The flattening formula** documented once and shared as a header the GL backend (ticket 10) uses.
- **The container format:** header (magic, format version, program name); per variant its key set, per stage the stage with its GL SPIR-V and the unflattened SPIR-V, reflection (vertex inputs; per set each binding's kind and name; the `MaterialParams` block in set 2, set 1 for 2D shaders, each member's name, type, std140 offset and size; texture bindings with names), the parameter defaults and the default block image. A reader for it, usable by the renderer core and FenrirAssets, and rejecting a wrong magic or version with a diagnostic.
- **Parameter defaults:** `#pragma fenrir default(<name>, <value>, ...)` beside the block or sampler; one to four float literals for `float`/`vec2`/`vec3`/`vec4`; `white`, `black` or `normal` for samplers; no pragma means zero or white silently; read per variant from preprocessed source. Build errors: unknown member, malformed pragma, wrong value count, two pragmas for one name in a stage, stages disagreeing, a default on an array, struct or matrix member. The default block image packs the defaults at std140 offsets and strides.
- **Engine includes** (`Fenrir/Frame.glsl`, `View.glsl`, `Draw.glsl`, `Vertex.glsl`, `2D.glsl`) with the spec's sets and bindings, leaving the reserved room for animation (two vertex locations after the standard ones, set 3 binding 1).
- **FenrirAssets:** `ShaderData` exposes its parameter table, and `GetShaderParameters(AssetHandle<Shader>)` returns `{ name, type, default }` from the container alone.

**Blocked by:** 03 (FenrirWindow over SDL3: the main window, its services and the input feed); 05 (FenrirAssets: the store, handles, owners, meshes and images).

**Touches:** `packages/FenrirRenderer/Tools/ShaderCompiler/`, `packages/FenrirRenderer/CMakeLists.txt`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirAssets/`, `.gitmodules`, `CMakeLists.txt`, `cmake/`

**Status:** ready-for-agent

- [ ] Shader-compiler fixture tests: the flattening formula on a fixture with every resource kind; `gl_InstanceIndex` refused; includes resolved; every combination of two variant keys built; set 2's reflection names, types, offsets and sizes as expected.
- [ ] Each pragma error fails the build of its fixture with a message naming the file and line; a pragma inside an `#ifdef` applies only to variants defining it; the default block image holds the defaults at std140 offsets (a `vec3` followed by a `float`, an array stride case).
- [ ] A container round-trips through the reader; a wrong magic or version is rejected with a diagnostic.
- [ ] `GetShaderParameters` on a fixture container returns every parameter with its type and default.
- [ ] Editing an included file rebuilds the containers that include it (depfiles); no glslang or SPIRV-Reflect include path reaches an engine target.
- [ ] Clang and MSVC x64 debug and release presets build the tool with no warnings from glslang's or SPIRV-Reflect's sources.
