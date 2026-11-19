# 08: Port the fullscreen and post programs to Slang

**What to build:** the programs drawn from `fullscreen.vert` move to Slang, one `fenrir_add_shaders` call at a time, each held to its goldens and probes: `fullscreen.vert`, `background`, `panorama`, `sky_irradiance`, `sky_specular`, `brdf_lut`, `bloom_downsample`, `bloom_upsample`, `lens_flare`, `gtao`, `hi_z`, `reflection_trace`, `scene_history` and `output` (about 1,500 lines of GLSL; ADR-0049, ADR-0056, ADR-0059). Their GLSL files are deleted as each port lands, so the tree never holds two copies of a program.

- **One file per program** where a program has one fragment stage: `Shaders/Builtin/<name>.slang` with a `[shader("fragment")]` entry point, and `fullscreen.slang` with the vertex entry point every one of them shares, named in `fenrir_add_shaders` as today. Keys stay as they are (`CUBEMAP`, `MSAA`, `HORIZONTAL`, `DIRT`, `DENOISE`, `COPY`, `DOWNSAMPLE`, `OCCLUSION`, `REFLECTIONS`, `ABERRATION`), as `#ifdef`s.
- **`output.frag`** is the largest (321 lines): the tonemappers mirrored in `src/Core/Tonemapping.cpp`, the colour grade, aberration, vignette and grain (ADR-0059), the reflection and occlusion debug views. Its port keeps every constant and every operation order, because the CPU tests pin the tonemappers to published values and the GL tests hold the shader to the CPU.
- **Order within the ticket:** `fullscreen.vert` and `background` first (the smallest, with a golden), then the sky filtering and the BRDF LUT (`SkyLighting.cpp` probes), bloom and lens flare (`Bloom.cpp` goldens), GTAO (`AmbientOcclusion.cpp`), Hi-Z, reflection trace and scene history (the reflection goldens), `output` last (`Tonemapping.cpp`, `Targets.cpp` and the LightingDemo goldens). Each lands as its own commit with its goldens green; a port that fails its goldens is fixed, never re-baked.
- **Header comments** carry over, naming the ADRs; `Fenrir/Lighting.glsl` references become `Fenrir.Lighting`.
- **Docs:** the README's "Materials and built-in shaders" and its pass list name no `.frag` files, so nothing changes there; `cmake/FenrirShaders.cmake`'s header comment loses "GLSL #version 460 in the Vulkan dialect" for "Slang, or GLSL #version 460 until ADR-0073's move completes".

**Blocked by:** 04 (Material defaults as attributes), 07 (Modules for lighting, shadows and skinning).

**Touches:** `packages/FenrirRenderer/Shaders/Builtin/`, `packages/FenrirRenderer/CMakeLists.txt`, `cmake/FenrirShaders.cmake`

**Status:** ready-for-agent

- [ ] Every golden image and GL probe under `packages/FenrirRenderer/Tests/OpenGL/` passes with no tolerance or golden changed, after each program's port and at the end.
- [ ] The CPU tonemapping tests and the GL tonemapping tests agree as before.
- [ ] No `.vert` or `.frag` remains for a ported program; `fenrir_add_shaders` names only `.slang` stages for them.
- [ ] Every ported program's GL copy passes `spirv-val` (ticket 05's test, extended to the built-ins' containers).
