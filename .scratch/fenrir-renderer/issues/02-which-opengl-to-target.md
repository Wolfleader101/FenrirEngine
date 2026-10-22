# Which OpenGL version and features should the backend target?

Type: research
Status: resolved
Blocked by: none

## Question

Which OpenGL core version, and which features of it, should FenrirRenderer's OpenGL backend require on Windows and Linux, given that macOS is out of scope and the seam must later admit D3D, Vulkan and Metal?

- **Versions:** what 4.5 (direct state access, `ARB_clip_control`) and 4.6 (`ARB_gl_spirv`, `ARB_spirv_extensions`) add, and their driver support on current NVIDIA, AMD and Intel on Windows and on Linux (Mesa).
- **Features that make GL look like an explicit API:** DSA, immutable storage (`glBufferStorage`, `glTextureStorage`), persistent mapped buffers, separate shader objects and program pipelines, sampler objects, `glClipControl` for a [0, 1] depth range and the Y flip, multi-draw indirect, bindless textures (extension).
- **SPIR-V in GL:** how usable `glSpecializeShader` is in practice (driver bugs, reflection, uniform names lost), against compiling GLSL source at runtime.
- **Debug:** `KHR_debug` output, object labels, debug groups (and RenderDoc's needs).
- **Loader and context:** glad 2 against alternatives; how the context is created (by the windowing library or by WGL and GLX directly); what a headless context for tests needs on Windows and Linux CI.

Write findings to a Markdown file on the `research/opengl-target` branch (`.scratch/fenrir-renderer/research/opengl-target.md`), citing each claim's source and pinning the versions read, and link it here.

## Answer

Findings: `.scratch/fenrir-renderer/research/opengl-target.md` on branch `research/opengl-target` (commit `f9bdcbc`, cut from `origin/main`), against the Khronos GL specs and registry, gpuinfo.org reports 17638, 17649, 17591, 17635, 17567 and 17624, Mesa release notes, RenderDoc 1.46 and glad v2.0.8, each claim cited.

- **Versions:** 4.5 made DSA and `glClipControl` core; 4.6 added SPIR-V shaders, `gl_DrawID` with indirect counts, anisotropic filtering, polygon offset clamp and `KHR_no_error`. Most explicit-style features are older: samplers 3.3, separate programs 4.1, immutable textures 4.2, `KHR_debug` and multi-draw indirect 4.3, buffer storage and persistent mapping 4.4.
- **Drivers:** current NVIDIA (Windows, Linux), AMD (Windows, Mesa radeonsi) and Intel (Windows, Mesa iris) all report 4.6 with SPIR-V, clip control, DSA and `KHR_debug`. Below 4.6 sit only old hardware (TeraScale, Fermi-era) and nouveau (4.5).
- **Software rendering for CI:** llvmpipe reports 4.5 up to Mesa 25.3 and 4.6 by Mesa 26.2; Ubuntu 24.04's Mesa gives 4.5, so a 4.6 floor needs a newer Mesa there. Mesa's d3d12 driver gives 4.6 on Windows over WARP with no GPU.
- **Explicit-style subset** (DSA, immutable textures and buffers, persistent mapping with fences, separate programs, samplers, clip control) is on every current driver and in RenderDoc, so a 4.5 floor needs no fallbacks. Clip control's upper-left origin and [0, 1] depth match D3D, Vulkan and Metal, but `glViewport` and other window-space calls stay lower-left, and images read back upside down.
- **Bindless textures:** never core; missing on Mesa iris, llvmpipe and d3d12, unsupported by RenderDoc 1.46; only an optional path with a texture-unit fallback.
- **SPIR-V in GL:** names lost (name queries fail), every location and binding explicit, no reflection API, no subroutines or mixing with GLSL source; the engine needs its own reflection (SPIRV-Cross); Mesa has never disk-cached SPIR-V shaders (issue #1929).
- **Shader routes:** runtime GLSL keeps driver reflection and caching; a middle route compiles offline to SPIR-V and emits GLSL for GL with SPIRV-Cross, keeping one source for later backends.
- **Debug:** full `KHR_debug` output only in a debug context, callbacks on other threads unless synchronous; RenderDoc supports core 3.2–4.6, assumes one rendering thread per frame, and supplies `KHR_debug` labels and groups itself.
- **Loader:** glad 2 (v2.0.8) generates from the Khronos XML; its multi-context option puts every GL function pointer in a per-context struct, keeping GL out of public headers (ADR-0010); its debug wrapper cannot combine with multi-context.
- **Context and headless CI:** GLFW 3.5.1 needs a (hidden) window for a context; SDL3's offscreen driver makes one through EGL with none; raw WGL needs a throwaway context first; EGL surfaceless needs no window system on Linux. Standard GitHub runners have no GPU, so CI needs Mesa: llvmpipe or EGL surfaceless on Linux, Mesa's `opengl32.dll` beside the executable on Windows (no official Mesa Windows builds; OSMesa removed in 25.1).
