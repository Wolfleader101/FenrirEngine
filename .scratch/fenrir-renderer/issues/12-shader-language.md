# Which shader language do we write, and how is it compiled for each backend?

Type: grilling
Status: resolved
Blocked by: 04, 08

## Question

Which language are FenrirRenderer's shaders (and users' shaders) written in, and how do they reach each backend? **ADR.**

- GLSL only for now, or one language translated per backend (GLSL or HLSL or Slang through SPIR-V)?
- Compiled at build time or at runtime; is hot reload wanted in the foundation?
- How are resource bindings and material parameters discovered (reflection or declared by hand)?
- Is the compiler a build-time tool only, or linked into the engine (and then `PRIVATE`)?

## Answer

ADR: `docs/adr/0017-fenrirrenderer-shaders-are-glsl-compiled-to-spir-v-at-build-time.md`, which also amends ADR-0016's OpenGL floor from 4.5 to 4.6. Glossary: Shader container, Shader variant, FenrirShaderCompiler.

- **Goal:** swapping OpenGL for Vulkan or D3D12 later must not touch shaders, materials or the renderer, so SPIR-V is the one compiled form of every shader from the start.
- **GL floor 4.6**, for SPIR-V ingestion (`glSpecializeShader`). Ubuntu 24.04's llvmpipe is 4.5, so headless GL tests in CI need a newer Mesa (ticket 20).
- **Language:** GLSL `#version 460` for every shader (engine, user, UI overlay), with explicit `set`/`binding`/`location`, uniforms in blocks only, [0, 1] depth and an upper-left origin.
- **Compiled at build time** to SPIR-V, the graphics device's only shader input. GL loads it directly; Vulkan takes it unchanged later; D3D12 and Metal get HLSL (then DXC) or MSL from SPIRV-Cross in the build. Fallback if GL's SPIR-V path hits driver bugs: SPIRV-Cross to GLSL in the same tool.
- **Layouts declared by hand** in C++ (binding groups, vertex layouts); debug builds check them against reflection the build writes.
- **`FenrirShaderCompiler`**, our own build-time executable (glslang, SPIRV-Reflect, SPIRV-Cross later), never linked into the engine; `fenrir_add_shaders()` runs it with depfiles. It flattens `set`/`binding` for GL's descriptor set 0, resolves `#include` against the engine's shared include directory, builds variants and writes reflection. How its libraries join the build: ticket 21.
- **One versioned shader container per program**, holding per-backend output and reflection; built-in shaders embedded in FenrirRenderer, users' shaders loaded as assets (ticket 15); the null backend accepts any and keeps nothing.
- **Variants:** `#define` keys, every combination built offline, no specialisation constants yet; which keys is ticket 17's call. What the shared include blocks hold: tickets 13 and 17.
- **No hot reload in the foundation.** Later, development builds run `FenrirShaderCompiler` as a child process and reload the container, still linking no compiler.
