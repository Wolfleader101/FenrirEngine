# How do engines write shaders once for several graphics APIs?

Type: research
Status: resolved
Blocked by: none

## Question

How do engines that target several graphics APIs write and compile their shaders, and what would each route cost FenrirRenderer, starting from OpenGL and later adding D3D, Vulkan or Metal?

- **Routes:** GLSL only; GLSL to SPIR-V (glslang or shaderc) and back to GLSL, HLSL and MSL with SPIRV-Cross; HLSL with DXC to DXIL and SPIR-V; Slang; WGSL with naga or Tint; a custom shading language (Godot, bgfx's shaderc).
- **When:** compiled at build time (CMake step, shipped bytecode) or at runtime (shipped source, cached), and hot reload in either.
- **Reflection:** discovering uniforms, resource bindings and vertex inputs to fill the renderer's material parameters, and the binding-model differences (GL's binding points against descriptor sets and root signatures).
- **Variants:** permutations (lit or unlit, skinned or not), specialisation constants, `#define`s.
- **Toolchain cost:** size, licence, build integration on Windows and Linux, and whether it can be a private dependency.

Write findings to a Markdown file on the `research/shader-languages` branch (`.scratch/fenrir-renderer/research/shader-languages.md`), citing each claim's source and pinning the versions read, and link it here.

## Answer

Findings: `.scratch/fenrir-renderer/research/shader-languages.md` on branch `research/shader-languages` (commit `103a200`), against glslang 16.6.0, shaderc, SPIRV-Cross, SPIRV-Reflect, DXC v1.9.2609, Slang v2026.20, naga (wgpu v30), Tint/Dawn, bgfx, Godot 4.7, Filament v1.78 and Bevy, each claim cited. It ends with open questions for "Which shader language do we write, and how is it compiled for each backend?".

- **glslang's HLSL front end is deprecated** (April 2026, removed at its next major; shaderc likewise), so any HLSL route needs DXC or Slang; bgfx's SPIR-V and WGSL paths still lean on it.
- **GLSL only costs nothing now:** the driver compiles and reflects. Written with explicit `layout(binding)`/`layout(location)` and no loose non-block uniforms (which Vulkan GLSL removes), it goes through glslang later without a rewrite.
- **GLSL → SPIR-V → SPIRV-Cross is the shared route** (bgfx, Filament, Godot's Vulkan, D3D12 and Metal backends): SPIR-V for Vulkan, MSL for Metal, HLSL for D3D still through FXC or DXC. Back to GL means flattening descriptor sets, combining textures and samplers, and specialisation constants as `#define`s (Filament's GL backend).
- **GL 4.6 SPIR-V ingestion** may drop names (`glGetUniformLocation` returns -1), needs explicit bindings and descriptor set 0, so reflection must come from SPIR-V (SPIRV-Cross or SPIRV-Reflect), not the driver.
- **HLSL with DXC** is strongest for D3D12 and solid for Vulkan; its SPIR-V is Vulkan-only, so GL still needs SPIRV-Cross; registers shifted with `-fvk-*-shift`; 32 MB Windows release or an LLVM-sized build.
- **Slang** has the best module, specialisation and reflection model, but its docs call GL support "limited", Metal and WGSL are experimental, D3D12 still goes through DXC, ~64 MB shared library, reflection only through its compile API.
- **WGSL** fits poorly: naga has no documented C API, Tint lives inside Dawn; no C++ engine read writes WGSL by hand.
- **A custom language** (Godot, bgfx) gives control (material offsets, variant caches, uniform tables in binaries) at the cost of maintaining a compiler, and still leans on glslang and SPIRV-Cross.
- **Variants are `#define` bitmasks** everywhere (Filament 38 of 64 keys; Godot GL compiles each combination on first use). Build-time compiling fits CMake through `add_custom_command` with `glslc -MD` depfiles (Visual Studio since CMake 3.21); hot reload needs a runtime compiler only in development builds.
- **Licences** are BSD, Apache-2.0, NCSA or MIT (glslang's GPL-3 text covers a removed Bison skeleton); all can stay `PRIVATE` under ADR-0010 if the public material API uses its own parameter types. glslang, SPIRV-Cross and SPIRV-Reflect link statically; DXC and Slang are shared libraries.
