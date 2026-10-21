---
status: accepted
---

# FenrirRenderer's shaders are GLSL, compiled to SPIR-V at build time

FenrirRenderer draws with OpenGL today, and Vulkan, D3D12 or Metal must be able to replace it without touching the code above the graphics device (ADR-0016). Shaders are where that is hardest to keep: every API wants its own language or binary. So every shader, the engine's, a user's or a UI overlay's, is written once in GLSL and compiled at build time to SPIR-V, and SPIR-V is the only shader input the graphics device accepts. OpenGL 4.6 loads that SPIR-V directly; later backends take the same bytes or what SPIRV-Cross makes of them.

## The decision

- **OpenGL 4.6 is the floor**, amending ADR-0016's 4.5: it adds SPIR-V ingestion (`glShaderBinary` with `glSpecializeShader`). Every current NVIDIA, AMD and Intel driver on Windows and Linux reports 4.6 (ticket 02).
- **One language: GLSL `#version 460`, written to rules that hold on every API.** Every resource has an explicit `layout(set = G, binding = B)`, set per binding group; every vertex input and stage output has an explicit `layout(location = N)`; uniforms live in blocks only, never loose. Shaders assume the device's conventions: [0, 1] depth and an upper-left origin.
- **Compiled at build time, never at runtime.** A shader is created on the device from SPIR-V bytes and a stage. The OpenGL backend specialises the SPIR-V; a Vulkan backend will take it unchanged; D3D12 and Metal will get HLSL (then DXC) or MSL from SPIRV-Cross, in the build. Adding a backend changes the build tool and the new backend, not shaders, materials or the renderer.
- **Layouts are declared, not reflected.** Pipeline creation takes a binding-group layout and a vertex layout declared in C++, as the explicit APIs require anyway (GL's SPIR-V path loses names, so there is no driver reflection to lean on). The build writes reflection beside the SPIR-V, and debug builds check the declared layout against it, failing pipeline creation with a diagnostic on a mismatch.
- **`FenrirShaderCompiler`, our own build-time tool.** An executable in FenrirRenderer's tree, linking glslang and SPIRV-Reflect (SPIRV-Cross once a second backend exists), never linked into the engine. A CMake function, `fenrir_add_shaders(<target> ...)`, runs it through `add_custom_command` with depfiles, for the engine's shaders and users' alike. Its jobs: compile, flatten each `set`/`binding` pair into one GL binding number for the GL output (GL's SPIR-V wants descriptor set 0), resolve `#include`, build variants, write reflection.
- **One shader container per program.** The tool writes one versioned file per shader program, holding each stage's output per backend (the GL-flattened SPIR-V for now) and its reflection. The renderer's own shaders are embedded in FenrirRenderer as byte arrays, so it runs with no files beside it; users' shaders are containers loaded as assets. The null backend accepts any container and keeps nothing.
- **Variants are `#define` keys, every combination built offline.** The CMake call names the keys; the container holds every combination. No specialisation constants yet: SPIRV-Cross lowers them to constants for HLSL and MSL anyway, and keys that add resources (a bone buffer) need `#define` regardless.
- **`#include`** uses `GL_GOOGLE_include_directive`, resolved by the tool against an include directory the engine ships, so engine and user shaders see the same shared blocks and conventions.

## Consequences

- **CI needs a newer Mesa for GL tests.** Ubuntu 24.04's llvmpipe reports 4.5, so headless GL tests need a Mesa that reports 4.6 (or Mesa's `opengl32.dll` on Windows), or they run only on hardware. The null backend is unaffected.
- **Shaders must exist at build time.** Shaders built from strings at runtime are not supported. Hot reload is not in the foundation; when it comes, development builds run `FenrirShaderCompiler` as a child process on a changed file and reload its container, still with no compiler linked into the engine.
- **SPIR-V on GL is less travelled than GLSL source.** Mesa never disk-caches SPIR-V shaders, so loads are slower on Linux and CI, which does not matter at our shader counts. If a driver bug bites, the tool can emit GLSL for GL through SPIRV-Cross instead, with no change above the graphics device.
- **Further than bgfx and Godot.** Both build around one source language, SPIR-V and SPIRV-Cross, but their GL backends still take GLSL. Ours takes the same SPIR-V a Vulkan backend will, so GL exercises the shipped format today.
- Libraries stay hidden as ADR-0010 asks: no public header names glslang, SPIRV-Reflect or SPIRV-Cross, and the engine links none of them. How they are fetched is the build ticket's call.

## Considered options

- **GLSL source compiled by the driver at runtime** (GL 4.5), with or without glslang as a build-time validator. Rejected: cheapest today, but the device would take source, so a Vulkan backend would change the device's interface or link a compiler into the engine.
- **GLSL to SPIR-V, then SPIRV-Cross back to GLSL for GL.** Kept only as the fallback above: it adds a step GL 4.6 does not need.
- **HLSL through DXC.** Rejected: strongest for D3D12, but its SPIR-V is Vulkan-only, GL still needs SPIRV-Cross, glslang's HLSL front end is deprecated, and DXC is a large shared library (research `research/shader-languages`).
- **Slang.** Rejected for now: the best module, specialisation and reflection model, but its GL support is documented as limited, D3D12 still goes through DXC, and it is a large shared library.
- **Reflection-driven layouts.** Rejected: the explicit APIs need layouts declared up front, and reflection becomes a debug check instead.

## When to revisit

If GL drivers' SPIR-V path proves unreliable in practice (take the SPIRV-Cross fallback), or variants outgrow building every combination offline.
