Status: ready-for-agent

# FenrirRenderer's shaders move to Slang: a second front end in FenrirShaderCompiler, the includes as modules, every program ported against its goldens, then glslang removed

Origin: the user's question "what would it take to move our shader language to Slang?", answered in `.scratch/fenrir-renderer/research/slang-migration.md` (read against Slang v2026.20 and `main` @ `6b94df2f`, with a prototype that compiled all 71 GLSL stages through Slang and ran the tool's `GlRewrite` on the result). ADR-0073 records the hard-to-reverse calls and supersedes ADR-0018; ADR-0016 (the private graphics device), ADR-0024 (the reflected material set), ADR-0048 (the lighting include), ADR-0049 (the depth prepass's equal test), ADR-0050 (`gl_Layer` for point shadows), ADR-0051 (the instance index and its spec constant), ADR-0056 (the view set's reflection bindings) and ADR-0057 (the indirection buffer and the palette's place) still apply and are amended where a ticket says so. The research file is the evidence for every claim here; this spec cites it rather than restating it.

## Problem statement

The shader code is 5,900 lines of GLSL: 30 built-in stages, 17 shared includes, three demos' shaders and the test fixtures. The includes are the engine's shader API, and nothing checks it: `Fenrir/Lighting.glsl` is a convention ("fill a `FenrirSurface`, call `FenrirLitColour`"), `Fenrir/Draw.glsl` relies on a binding-order rule ADR-0057 writes down because the preprocessor cannot enforce it, and every stage of every variant re-parses all of it. A second backend (ADR-0016) would need SPIRV-Cross for HLSL and MSL. Slang gives modules, interfaces, attributes and one reflection API, and emits HLSL, Metal and WGSL from one source.

The prototype found that the obvious ways in are wrong. Slang's GLSL mode drops a vertex stage's `gl_Layer` write silently, has no `invariant`, and refuses `mat4(mat3)`; its default matrix layout reads every uploaded matrix transposed; at SPIR-V 1.0 it leaves capabilities' extensions out; and it strips unused resources, which on today's `main` moves ADR-0057's indirection buffer from GL point 27 to 26 in every non-`SKINNED` mesh variant. Each has a fix, and each would ship wrong pixels or invalid SPIR-V without one.

## Solution

`FenrirShaderCompiler` gains a second front end: `.slang` stages go through Slang's compilation API, `.vert`, `.frag` and `.comp` through glslang as now, and both reach the same reflection model, `GlRewrite` and shader container. Slang is a pinned prebuilt release fetched at configure time, linked by the tool alone. Reflection for Slang stages comes from Slang's program layout, which lists every declared resource; the tool compiles with column-major matrices, completes the SPIR-V 1.0 that GL takes (the draw-parameters extension, the layer capability, the invariant position), and keeps ADR-0018's rules as checks on reflection with their messages. `#pragma fenrir default` becomes `[FenrirDefault]` and `[FenrirDefaultTexture]`. Variants stay `#define` keys.

The seventeen includes become modules under `Fenrir.` in the same directory, with the GLSL includes kept in step beside them while any GLSL program uses them. Then each built-in program is ported in groups (fullscreen and post, mesh, compute and debug and 2D), each held to its golden images and GL probes before the next group; then the demos' and tests' shaders; then glslang, SPIRV-Reflect, the GLSL path and the GLSL includes leave, and the docs follow.

## User stories

1. As an engine developer, I want to write a new built-in in Slang and have `fenrir_add_shaders` build it beside the GLSL ones, so that the move can start without a flag day.
2. As an engine developer, I want a Slang stage's bindings, block members and vertex inputs in the container exactly as a glslang stage's would be, so that the renderer, the material set and C# notice nothing.
3. As an engine developer, I want the tool to refuse a Slang stage that breaks ADR-0018's rules (no set and binding, a push constant, nine textures in a set, the instance index outside `Fenrir.Draw`) with a message naming the file and line, so that the rules outlive glslang's AST.
4. As an engine developer, I want the GL copy of a Slang stage to pass `spirv-val` and to bind every resource where glslang's copy did, so that a ported program draws into the same binding points.
5. As a shader author, I want `import Fenrir.Lighting;` to give me `FenrirSurface` and its lit colour, so that a material's shader reads as code, not as a convention.
6. As a shader author, I want `[FenrirDefault(1, 1, 1, 1)]` on a parameter and `[FenrirDefaultTexture("white")]` on a texture to be my material's defaults, so that defaults sit on the declaration they describe.
7. As a game developer, I want my GLSL shaders to keep building while the engine's move on, and to know the release that stops accepting them, so that I port on my own schedule.
8. As a reviewer, I want each ported program's goldens and probes to pass unchanged in tolerance, so that a port is a refactor and not a look change.

## Build order

Tickets whose Touches lines share a file are a stack (docs/agents/issue-tracker.md). Here that is almost everything, in two stacks that meet:

- **The tool:** 01 → 02 → 03 → 04 → 05, each on the one before, all under `packages/FenrirRenderer/Tools/ShaderCompiler/` and `cmake/`.
- **The modules:** 06 → 07, both under `packages/FenrirRenderer/Shaders/include/`, cut from 05.
- **The ports:** 08 → 09 → 10 → 11, each on the one before, since they share `packages/FenrirRenderer/CMakeLists.txt` and the GL tests; 08 is cut from 07.
- **The removal:** 12, last.

Build only the msvc-x64-debug preset locally; CI covers the rest. A ticket's PR targets the ticket's branch below it, and the stack goes into `main` through an integration branch, `fenrir-renderer-slang`.

## Open questions settled by the ADR

Why now (the include set has become an unchecked language; a second backend); how Slang is fetched (a prebuilt release at configure time); the SPIR-V version for GL (1.0, completed by the tool); where reflection comes from (Slang's layout); the defaults' syntax (attributes); the variant model (`#define` keys); the rules and how each is enforced; module layout and naming (`Fenrir.<Include>`, source shipped, no precompiled modules); user GLSL shaders (accepted until the last built-in is ported, then one release more); future backends (the new backend's ADR).

## Not in this spec

An `IFenrirMaterial` interface; link-time or specialization constants; a newer SPIR-V version on GL; hot reload; HLSL, Metal or WGSL in the container; `ParameterBlock<T>` for the material set. Each is in ADR-0073's "Not yet".

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | Slang fetched at configure time, linked by the tool only | |
| 02 | The Slang front end in FenrirShaderCompiler | 01 |
| 03 | Reflection and the rules from Slang's program layout | 02 |
| 04 | Material defaults as attributes | 03 |
| 05 | The GL copy completed: draw parameters, layer, invariant, spirv-val | 02 |
| 06 | Modules for the sets, the draw and the vertex: Fenrir.Frame to Fenrir.Constants | 03, 05 |
| 07 | Modules for lighting, shadows and skinning | 06 |
| 08 | Port the fullscreen and post programs | 04, 07 |
| 09 | Port the mesh programs | 08 |
| 10 | Port the compute, debug and 2D programs | 09 |
| 11 | Port the demos' and tests' shaders | 10 |
| 12 | Remove glslang, SPIRV-Reflect, the GLSL path and the GLSL includes; the docs | 11 |
