# Which OpenGL version and features the backend could target

Research for [02-which-opengl-to-target](../issues/02-which-opengl-to-target.md). This file reports what OpenGL 4.5 and 4.6 contain, what current drivers expose on Windows and Linux, and what each choice costs. It lists options and trade-offs and does not choose a version.

Read on 2026-10-21. Sources and the versions read:

- **Khronos OpenGL 4.6 Core Profile specification, May 5, 2022** ([glspec46.core.pdf](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf)). Appendices G to J list what each version from 4.3 to 4.6 added.
- **Khronos extension specs** in the OpenGL registry, at the revisions they carry:
  - [ARB_direct_state_access](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_direct_state_access.txt), rev 52, May 8, 2025
  - [ARB_clip_control](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_clip_control.txt), rev 19, 2018-04-06
  - [ARB_gl_spirv](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_gl_spirv.txt), rev 46, August 19, 2020
  - [ARB_buffer_storage](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_buffer_storage.txt), rev 25, April 20, 2015
  - [ARB_texture_storage](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_texture_storage.txt), rev 26, August 7, 2013
  - [ARB_separate_shader_objects](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_separate_shader_objects.txt), 1 August 2011
  - [ARB_sampler_objects](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_sampler_objects.txt)
  - [ARB_multi_draw_indirect](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_multi_draw_indirect.txt), rev 4, June 9, 2014
  - [ARB_indirect_parameters](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_indirect_parameters.txt), rev 5, 11 November 2020
  - [ARB_shader_draw_parameters](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_shader_draw_parameters.txt), rev 4, 29 May 2017
  - [ARB_bindless_texture](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_bindless_texture.txt), rev 5, February 13, 2017
  - [KHR_debug](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_debug.txt), rev 17, July 2, 2015
  - [WGL_ARB_create_context](https://registry.khronos.org/OpenGL/extensions/ARB/WGL_ARB_create_context.txt) and [EGL_MESA_platform_surfaceless](https://registry.khronos.org/EGL/extensions/MESA/EGL_MESA_platform_surfaceless.txt)
- **Mesa `docs/features.txt`** at commit [`255ecc7e`](https://gitlab.freedesktop.org/mesa/mesa/-/blob/255ecc7e5837b2824d56f2d24ce500ae96313a83/docs/features.txt) (2026-10-20, `main`). Line numbers below refer to that commit. Also Mesa docs at docs.mesa3d.org (latest), [Mesa 25.1.0 release notes](https://docs.mesa3d.org/relnotes/25.1.0.html), and Mesa issues on gitlab.freedesktop.org.
- **gpuinfo.org OpenGL database** ([opengl.gpuinfo.org](https://opengl.gpuinfo.org)), reports up to id 17654, queried 2026-10-21. Reports are cited by id; each id opens as `https://opengl.gpuinfo.org/displayreport.php?id=<id>`.
- **RenderDoc** docs ([OpenGL support page](https://renderdoc.org/docs/behind_scenes/opengl_support.html), v1.47 docs branch) and source at tag [`v1.46`](https://github.com/baldurk/renderdoc/blob/v1.46/renderdoc/driver/gl/gl_driver.cpp) (the latest release, 2026-08-31).
- **glad** branch `glad2` @ [`0a7cf1a`](https://github.com/Dav1dde/glad/tree/0a7cf1af9e25e02821587bd4095807fc90b6426a) (latest release v2.0.8), and its [C wiki page](https://github.com/Dav1dde/glad/wiki/C).
- **glslang** `main` @ [`5a0d625`](https://github.com/KhronosGroup/glslang/tree/5a0d62504b8126220a924613a902e620351d5ebf) (latest release 16.6.0) and **SPIRV-Cross** `main` @ [`300681b`](https://github.com/KhronosGroup/SPIRV-Cross/tree/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1), from their READMEs.
- **GLFW 3.5.1** docs ([context guide](https://www.glfw.org/docs/latest/context_guide.html), [window guide](https://www.glfw.org/docs/latest/window_guide.html), [intro guide](https://www.glfw.org/docs/latest/intro_guide.html)). **SDL** at tag [`release-3.4.18`](https://github.com/libsdl-org/SDL/tree/release-3.4.18).
- Microsoft Learn ([wglGetProcAddress](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglgetprocaddress)) and GitHub Docs ([GitHub-hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)).

NVIDIA, AMD and Intel publish no per-extension support tables for their GL drivers, so their support is shown here from gpuinfo.org reports. A report is one driver on one GPU and shows what that driver exposed, not a vendor promise.

---

## 1. What 4.5 and 4.6 add, and who ships them

### What each version adds

- **OpenGL 4.5** added these to core ([spec Appendix I.1, p. 718–719](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf)):
  - `ARB_clip_control` and `ARB_direct_state_access`, the two the ticket names
  - `ARB_cull_distance`, `ARB_ES3_1_compatibility`, `ARB_conditional_render_inverted`, `KHR_context_flush_control`, `ARB_derivative_control`, `ARB_get_texture_sub_image`, `KHR_robustness`, `ARB_shader_texture_image_samples` and `ARB_texture_barrier`
- **OpenGL 4.6** added these ([Appendix J.1, p. 733–734](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf)):
  - `ARB_gl_spirv` and `ARB_spirv_extensions` (SPIR-V shaders)
  - `ARB_indirect_parameters` (the draw count of a multi-draw-indirect read from a buffer) and `ARB_shader_draw_parameters` (`gl_DrawID`, `gl_BaseVertex`, `gl_BaseInstance` in GLSL)
  - `ARB_texture_filter_anisotropic`, which was an EXT extension before 4.6 although nearly every driver exposed it
  - `ARB_polygon_offset_clamp` (useful for shadow-map bias), `KHR_no_error`, `ARB_pipeline_statistics_query`, `ARB_shader_atomic_counter_ops`, `ARB_shader_group_vote` and `ARB_transform_feedback_overflow_query`
- **Most "explicit-looking" features are older than 4.5:**
  - sampler objects are core since 3.3
  - separate shader objects and program pipelines since 4.1
  - immutable texture storage (`glTexStorage*`) since 4.2 ([Appendix F.1](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf))
  - `KHR_debug` and `ARB_multi_draw_indirect` since 4.3 ([G.2](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf))
  - `ARB_buffer_storage` (immutable buffers and persistent mapping) since 4.4 ([H.1](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf))

  4.5 adds the DSA forms of all of these (`glCreate*`, `glNamedBufferStorage`, `glTextureStorage2D`, `glTextureSubImage2D` and so on) and `glClipControl`.

### Mesa (Linux; also Windows through its d3d12 and llvmpipe drivers)

[features.txt @ 255ecc7e](https://gitlab.freedesktop.org/mesa/mesa/-/blob/255ecc7e5837b2824d56f2d24ce500ae96313a83/docs/features.txt):

- **GL 4.5 is complete on** freedreno/a6xx, nvc0 (nouveau), r600, radeonsi, llvmpipe, virgl, zink, iris, crocus/gen7.5+, d3d12 and asahi (L214).
- **GL 4.6 is complete on** radeonsi, virgl, zink, iris, crocus/gen7+, d3d12, asahi, r600/evergreen+ and llvmpipe (L229).
  - **nvc0 (nouveau GL on NVIDIA) is not in the 4.6 list.** The Mesa route to GL 4.6 on NVIDIA is zink over NVK; gpuinfo report 17111 shows "zink Vulkan 1.4 (… NVK)" at GL 4.6 on Linux.
- **By hardware:**
  - AMD on Linux runs radeonsi (GL 4.6).
  - Intel Gen8 and newer, including Arc, runs iris (GL 4.6). Report 17624 shows a Mesa Intel Arc A380 at "4.6 (Core Profile) Mesa 26.2.4".
- **llvmpipe** (the CPU rasterizer used for headless CI):
  - It reached GL 4.6 recently. gpuinfo reports show "4.5 (Core Profile)" on every Mesa release up to Mesa 25.3.0 (report 16012), and "4.6 (Core Profile) Mesa 26.2.0" from report 17255 onwards.
  - The Ubuntu 24.04 package "Mesa 25.2.8-0ubuntu0.24.04.2" reports llvmpipe at **4.5** (report 17040). Mesa 26.0 and 26.1 were not checked.
- **d3d12** (Mesa's "GLOn12", GL over Direct3D 12 on Windows):
  - Report 17645 shows "D3D12 (Microsoft Basic Render Driver)", that is D3D12 over WARP with no GPU, at "4.6 (Core Profile) Mesa 27.0.0-devel" on Windows 10.
  - The [d3d12 driver page](https://docs.mesa3d.org/drivers/d3d12.html) still says "full desktop OpenGL 3.3 support"; features.txt and the reports show it is now 4.6.

### Proprietary drivers

What current drivers on gpuinfo.org report:

| Vendor, OS | Example report | GL_VERSION |
|---|---|---|
| NVIDIA, Windows | 17638, RTX 5080 | `4.6.0 NVIDIA 617.70` |
| NVIDIA, Linux | 17649, RTX 4070 | `4.6.0 NVIDIA 615.78.08` |
| AMD, Windows | 17591, RX 6700 XT | `4.6.0 Core Profile Context 26.9.2.260915` |
| AMD, Linux (Mesa radeonsi) | 17635, Radeon 840M | `4.6 (Compatibility Profile) Mesa 26.2.4` |
| Intel, Windows | 17567, Arc B580 | `4.6.0 - Build 32.0.101.9033` |
| Intel, Linux (Mesa iris) | 17624, Arc A380 | `4.6 (Core Profile) Mesa 26.2.4` |

Every one of these reports exposes `GL_ARB_gl_spirv`, `GL_ARB_spirv_extensions`, `GL_ARB_clip_control`, `GL_ARB_direct_state_access`, `GL_KHR_debug`, `GL_ARB_indirect_parameters`, `GL_ARB_polygon_offset_clamp` and `GL_ARB_texture_filter_anisotropic`. These were checked by reading each report.

**The hardware that stops below 4.6 is old.** Among reports with a 4.5+ context that lack bindless:

- AMD TeraScale GPUs on Windows (Radeon HD 6370D, HD 7500 series) report `4.5.13399` (reports 17183, 16879).
- Fermi-era NVIDIA Quadro 600 and 4000 report `4.5.0` (reports 15670, 15909).
- Mesa nouveau (nvc0) stops at 4.5, as above.

### Choosing 4.5 or 4.6 as the floor

- **A 4.5 floor:**
  - It adds only the AMD TeraScale, Fermi-era NVIDIA and nouveau GPUs above, plus llvmpipe on Mesa 25.3 and older. That last group matters for Linux CI on Ubuntu 24.04 (see section 5).
  - SPIR-V, `gl_DrawID` and indirect counts become optional extensions, checked at runtime.
- **A 4.6 floor:** it is what every current desktop driver above reports, including Mesa's software paths on recent Mesa, and makes SPIR-V ingestion and `gl_DrawID` core.
- **A middle path:** require a 4.5 context and use 4.6 features (or their ARB extensions) when present. This costs a capability check in the backend for each such feature.

## 2. Features that make GL look like an explicit API

| Feature | Core since | What it gives | Notes |
|---|---|---|---|
| Direct state access | 4.5 | Edit objects by name, without binding them | `glCreate*` makes objects without a bind, so there is no "bind-to-create" ([ARB_direct_state_access, Overview](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_direct_state_access.txt)). Mesa marks it DONE on all drivers (features.txt L221). |
| Immutable textures (`glTexStorage*`, `glTextureStorage*`) | 4.2 (DSA forms 4.5) | Size, format and mip count fixed at creation, like a `VkImage` | Mesa: all drivers (L158 for `ARB_texture_storage`). |
| Immutable buffers (`glBufferStorage`, `glNamedBufferStorage`) | 4.4 | Size and usage flags fixed at creation | "an implementation … may be able to make certain assumptions or apply particular optimizations" ([ARB_buffer_storage, Overview](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_buffer_storage.txt)). |
| Persistent mapped buffers (`MAP_PERSISTENT_BIT`, `MAP_COHERENT_BIT`) | 4.4 | Keep a CPU pointer mapped while the GPU draws from the buffer, the basis of ring-buffered uploads | Synchronisation is the application's job (see below). |
| Separate shader objects and program pipelines | 4.1 | Mix and match stages, as Vulkan pipelines, D3D and Metal do | Program pipelines are container objects "not shared between contexts" ([ARB_separate_shader_objects, Overview](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_separate_shader_objects.txt)). `ProgramUniform*` updates a program without binding it. Mesa: all drivers (L147). |
| Sampler objects | 3.3 | Sampling state apart from texture data, as in every other API | One sampler can serve several units ([ARB_sampler_objects, Overview](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_sampler_objects.txt)). Mesa: all drivers (L105). |
| `glClipControl(UPPER_LEFT, ZERO_TO_ONE)` | 4.5 | D3D, Vulkan and Metal clip-space conventions: depth in [0, 1], Y flipped | See below. |
| Multi-draw indirect | 4.3; indirect count and `gl_DrawID` 4.6 | GPU-driven draws from a buffer of draw commands | Mesa: all drivers that support `ARB_draw_indirect` (L182); `ARB_indirect_parameters` and `ARB_shader_draw_parameters` are part of 4.6 (L232, L236). |
| Bindless textures (`ARB_bindless_texture`) | never core | 64-bit texture handles used in shaders without texture units | See below. Not on Mesa iris, llvmpipe or d3d12, and not supported by RenderDoc. |

### Clip control

- `ZERO_TO_ONE` maps NDC z to [0, 1], as in D3D, Vulkan and Metal. The spec gives two reasons: to "allow OpenGL to effectively match Direct3D's coordinate system conventions", and that it can "potentially improve the numerical precision of the Z coordinate mapping", mainly with floating-point depth buffers ([ARB_clip_control, Overview and Issues](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_clip_control.txt)).
- `UPPER_LEFT` negates clip-space Y. It also negates the polygon-area sign, so face culling is unchanged.
- **Window-space commands keep a lower-left origin.** `glViewport`, `glScissor` and `glWindowPos` are unchanged, so a D3D-style viewport has to be flipped by hand, and the spec gives the formula.
- **Rendering under `UPPER_LEFT` stores images upside down from GL's point of view.** Render targets then come out in D3D orientation and D3D-style texture coordinates work unmodified, but the default framebuffer comes out flipped and needs a flipped blit or viewport when presenting.
- **Mesa coverage:** every driver at GL 4.5 has it, plus freedreno, nv50, softpipe, lima, panfrost, crocus and v3d (L217).
- **The shader-side alternative:** SPIRV-Cross can rewrite `gl_Position` from [0, w] depth into GL's [-w, w] (`fixup_clipspace`) and can flip Y (`flip_vert_y`). Its README recommends changing the projection matrices instead, and says vertex-shader Y-flipping "tends to get quite messy" ([SPIRV-Cross README, "Clip-space conventions"](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/README.md)).

### Persistent mapping

The [ARB_buffer_storage](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_buffer_storage.txt) spec sets these rules:

- **CPU writes:**
  - Without `MAP_COHERENT_BIT`, they must be flushed with `FlushMapped*BufferRange` before the GPU sees them.
  - With `MAP_COHERENT_BIT`, the GPU sees them in later commands without a flush.
- **GPU writes:** the CPU sees them only after `FenceSync(SYNC_GPU_COMMANDS_COMPLETE)` has completed, plus `MemoryBarrier(CLIENT_MAPPED_BUFFER_BARRIER_BIT)` when the mapping is not coherent.
- **The cost:** the application has to fence each region before overwriting it. That is the same frame-in-flight bookkeeping that Vulkan and D3D12 upload rings need, so it maps directly onto those backends.

### Bindless textures

- **What it does:** a shader reads textures through 64-bit handles, without texture units.
- **What it requires:**
  - Handles must be made resident before use. Using a non-resident handle "can result in undefined results, including program termination".
  - Once a handle has been taken from a texture, "structural changes (e.g., image size and format and parameter changes) … are not possible for the lifetime of the texture".
  - Residency is managed per context ([ARB_bindless_texture, Overview and Issues 5–6](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_bindless_texture.txt)).
- **Who exposes it:**
  - Mesa: only nvc0, radeonsi and zink (features.txt L301). **iris (all Intel on Linux), llvmpipe and d3d12 do not.** Reports 17624, 17599 and 17645 show `GL_ARB_bindless_texture` absent on all three.
  - The proprietary NVIDIA, AMD and Intel Windows drivers above all expose it (reports 17638, 17649, 17591, 17567), and so does Mesa radeonsi (17635).
- **gpuinfo coverage over all reports since 2011:** 52.63% for `GL_ARB_bindless_texture`, against 69.59% for `GL_ARB_direct_state_access` and 95.05% for `GL_KHR_debug`. These figures include very old hardware, so they show how widely an extension is exposed relative to the others, not its share among current GPUs.
- **RenderDoc** lists `GL_ARB_bindless_texture` among "Extensions I plan to support, but haven't implemented yet" ([gl_driver.cpp @ v1.46, L270–L286](https://github.com/baldurk/renderdoc/blob/v1.46/renderdoc/driver/gl/gl_driver.cpp#L270-L286)). It reports only the intersection of its own list and the driver's (comment at L270–L273), so an application under RenderDoc sees no bindless.
- **The consequence:** bindless can only be an optional path with a texture-unit fallback. The fallback is needed on Intel Linux, on CI software rasterizers and whenever RenderDoc is attached.

## 3. SPIR-V in GL against GLSL source at runtime

### What `ARB_gl_spirv` requires (rev 46)

- **Loading:** the module is passed to `glShaderBinary` with `SHADER_BINARY_FORMAT_SPIR_V`. `glSpecializeShader` then picks the entry point and sets specialization constants, before linking (§7.2, §7.2.1).
- **No names, so no name-based reflection.** SPIR-V variables "may not have names associated with them" (§7.3.1). Issue 19 resolved that name-based queries fail while count and type queries still work, though implementations may still return debug names. The effects:
  - `glGetUniformLocation` and `glGetAttribLocation` return -1.
  - `glGetProgramResourceIndex` returns `INVALID_INDEX`.
  - Name lengths return 1 (Issues 19, 22).
  - The extension adds no reflection API (Issue 8 deferred one).
- **Everything must be explicitly placed:**
  - All `in` and `out` variables and blocks need a `location`, and uniform blocks and SSBOs need a `binding` (§4.4, §7.4.spv).
  - Default-block uniforms need explicit locations and are set by location (Issue 23).
  - Uniform block bindings cannot be remapped with `glUniformBlockBinding` (Issue 24).
  - Sampler and image bindings are fixed in the shader (Issue 26).
  - `glBindAttribLocation` and `glBindFragDataLocation` have no effect, and transform-feedback varyings by name are not supported (§11.1.1.1, §15.2.3.1, Issue 10).
- **Not available in SPIR-V for GL** (§6.1.2, §7.6.2.spv and Appendix A.spv):
  - subroutines
  - `shared` and `packed` block layouts
  - built-in uniform state and noise functions
  - Vulkan-only features: push constants, input attachments, separate `OpTypeSampler`, `VertexIndex` and `InstanceIndex`. `DescriptorSet` must be 0.

  Default uniforms (outside blocks) remain allowed.
- **No mixing:** a program cannot mix SPIR-V and GLSL-source shaders (Issue 17), and only fully linked single modules are accepted (Issue 6).
- **Specialization constants** are scalar int, float or bool, and a shader can be specialized only once unless its module is passed again (§7.2.1).

### Problems seen in practice

- **Mesa never caches SPIR-V shaders on disk.**
  - Mesa issue [#1929](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/1929), "ARB_gl_spirv: shader cache support", has been open since 2019-10-11. It says the SPIR-V path "doesn't support shader cache" and that SPIR-V shaders are kept out of the cache explicitly.
  - GLSL source shaders do use Mesa's on-disk cache (`MESA_SHADER_CACHE_DISABLE`, [envvars](https://docs.mesa3d.org/envvars.html)).
  - So on Linux with Mesa, SPIR-V can mean slower start-up on every run than GLSL source does.
- **Mesa's work is unfinished.** Meta-issue [#1935](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/1935) (open since 2019) tracks eight `ARB_gl_spirv` items and one `ARB_spirv_extensions` item, described as "really specific corner cases".
  - Closed issue [#7644](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/7644) (fixed 2023-02) found that `GL_SHADER_BINARY_FORMAT_SPIR_V` was missing from `GL_SHADER_BINARY_FORMATS` on i965 and radeonsi.
  - Closed issue [#3447](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/3447) discussed zink's handling of SPIR-V shaders. Its author notes that the path converts SPIR-V to NIR, back to SPIR-V and to NIR again.
- **An NVIDIA report, lower trust:** on NVIDIA's own developer forum, a user reported that driver 430.40 left specialization-constant values out of its shader-cache hash, so a second specialization reused the first ([NVIDIA forum thread](https://forums.developer.nvidia.com/t/shader-disk-cache-bug-with-opengl-4-6-spir-v-specialization/80744)). It is a user report from 2019; this research did not check whether it was fixed.
- **Support is near universal at 4.6.** Among gpuinfo reports with a 4.6 context, the latest 2,000 reports that lack `GL_ARB_gl_spirv` include only four 4.6 reports: one radeonsi, two Panfrost and one virgl. All the current drivers in section 1 expose it.

### Reflection, if SPIR-V is used

- **The engine needs its own reflection.** Since the driver cannot give names, the engine has to get uniform and binding layouts from a tool:
  - SPIRV-Cross offers a reflection API and a JSON reflection output. Its reflection "will follow the exact name specified in the SPIR-V module", which holds names when the compiler kept debug info ([SPIRV-Cross README](https://github.com/KhronosGroup/SPIRV-Cross/blob/300681bbcb8365a8d1eacdacdbdd0249bbf1d6b1/README.md)).
  - glslang's own reflector works "from the HLL source (not the SPIR-V)" and is "only approximate for what would later be emitted for SPIR-V" ([glslang README, "Reflector"](https://github.com/KhronosGroup/glslang/blob/5a0d62504b8126220a924613a902e620351d5ebf/README.md)).
- **Compiling GLSL to SPIR-V for GL:** glslang's API takes `EShClientOpenGL` with `EShTargetOpenGL_450` (same README, "C++ Class Interface").

### The alternative: GLSL source at runtime

- **What it keeps:** names and `glGetProgramResource*` introspection, the Mesa disk cache, and subroutines.
- **What it costs:** a GLSL compile in the driver, so driver-specific front-end quirks.
- **A middle route that keeps one shader source for all future backends:** compile the shaders offline to SPIR-V, then use SPIRV-Cross to emit GLSL 4.x for the GL backend. SPIRV-Cross also emits HLSL and MSL, and its README covers flattening Vulkan descriptor sets to GL's flat bindings and combining image and sampler pairs for GLSL.
- **For the shader-language ADR (it does not decide this ticket):** glslang's HLSL front-end has been "Deprecated as of April 2026" and will be removed at the next major version, with at least 18 months' notice ([glslang README](https://github.com/KhronosGroup/glslang/blob/5a0d62504b8126220a924613a902e620351d5ebf/README.md)).

## 4. Debug output, labels, groups and RenderDoc

### What `KHR_debug` provides (core since 4.3)

- **Messages:** delivered to a callback, each with a source, type, ID and severity ([KHR_debug, Overview](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_debug.txt)).
- **Labels:** `glObjectLabel` and `glObjectPtrLabel` name objects, up to `MAX_LABEL_LENGTH`.
- **Groups:** `glPushDebugGroup` and `glPopDebugGroup` mark nested regions, up to `MAX_DEBUG_GROUP_STACK_DEPTH`.
- **A debug context is needed for full output.** In a context created with `CONTEXT_FLAG_DEBUG_BIT`, `DEBUG_OUTPUT` starts enabled. Without the flag it starts disabled, and if enabled later "the level of debug output logging is defined by the GL implementation, which may have zero debug output" (§5.5). In practice the backend would ask for a debug context in debug builds.
- **Callbacks can arrive on other threads unless synchronous output is on.** While `DEBUG_OUTPUT_SYNCHRONOUS` is off (the default), the driver may call the callback "from potentially multiple threads" and after the GL call has returned. Turning it on gives callbacks on the calling thread before the call returns, so a breakpoint in the callback shows the faulting call (§5.5, around L745–L765).
- **Coverage:** Mesa marks `KHR_debug` DONE on all drivers (L176). Every current report in section 1 exposes it. gpuinfo coverage over all reports is 95.05%.

### RenderDoc

From the [OpenGL support page](https://renderdoc.org/docs/behind_scenes/opengl_support.html) (v1.47 docs) and [`gl_driver.cpp` @ v1.46](https://github.com/baldurk/renderdoc/blob/v1.46/renderdoc/driver/gl/gl_driver.cpp):

- **Core profile only:** "RenderDoc only supports the core profile of OpenGL – from 3.2 up to 4.6 inclusive". Compatibility contexts capture but may not replay.
- **One rendering thread per frame:** RenderDoc assumes a frame's commands come from a single thread, and "interleaved rendering commands from multiple threads will not work". Loading on other threads is supported.
- **Debug markers are always available under RenderDoc.** It adds `GL_KHR_debug` and `GL_EXT_debug_label` to the extension string even when the driver lacks them (L1535–L1546). Labels and groups are what RenderDoc shows in its event browser and resource names.
- **Explicit-style features are supported; bindless and sparse are not.**
  - RenderDoc's list of supported extensions includes `ARB_buffer_storage`, `ARB_clip_control`, `ARB_direct_state_access`, `ARB_gl_spirv`, `ARB_spirv_extensions`, `ARB_indirect_parameters`, `ARB_multi_draw_indirect`, `ARB_sampler_objects`, `ARB_separate_shader_objects`, `ARB_shader_draw_parameters` and `ARB_texture_storage` (L45–L171).
  - `ARB_bindless_texture`, `ARB_sparse_texture` and `ARB_sparse_buffer` are on its "plan to support, but haven't implemented yet" list (L270–L286).
- **Shader debugging** in RenderDoc needs at least `ARB_compute_shader` and `ARB_shader_storage_buffer_object`, both core since 4.3.

## 5. Loader and context

### glad 2

- **How it loads:** glad 2 generates loaders from the official XML specs for GL, GLES, EGL, GLX, WGL and Vulkan ([README](https://github.com/Dav1dde/glad/blob/0a7cf1af9e25e02821587bd4095807fc90b6426a/README.md)). It is driven by the windowing library's `getProcAddress`: `gladLoadGL(glfwGetProcAddress)` returns the loaded version.
- **The options that matter here** ([C wiki](https://github.com/Dav1dde/glad/wiki/C)):
  - **`loader`** adds a built-in loader (`gladLoaderLoadGL()`) that does not depend on the windowing library. The wiki advises using the windowing library's loader if there is one.
  - **`mx`** (multi-context) puts every function pointer in a `GladGLContext` struct, so calls become `gl->Clear(...)`. That keeps GL symbols out of the global namespace and gives one table per context. With "mx global", a global instance is generated as well.
  - **`debug`** wraps every call with pre- and post-callbacks, for example to check `glGetError` after each call. It **cannot be combined with `mx`**.
  - **`alias`** resolves ARB and EXT aliases of core functions at load time.
- **Build integration:** the generated files can be checked in (`src/gl.c`, `include/glad/gl.h`, `include/KHR/khrplatform.h`). Alternatively `glad_add_library(... API gl:core=4.6)` generates them at configure time from a glad submodule, which needs a Python interpreter during the build ([wiki, "CMake"](https://github.com/Dav1dde/glad/wiki/C)).
- **Maintenance:** the last release is v2.0.8 (2024-09-29); the `glad2` branch was last pushed 2026-09-28.
- **The current DemoApp** already links a `glad` subdirectory and asks GLFW for a 4.6 core context (`examples/DemoApp/src/Window.cpp` L26–L28). The `libs/glad` sources are not in the tree at `cf09b0f`.

### Alternatives (latest releases, from GitHub)

- **GLEW:** glew-2.3.1, 2026-01-24.
- **libepoxy:** 1.5.10, 2022-03-18. It handles GLX, EGL and WGL dispatch itself.
- **glbinding:** v3.5.0, 2025-04-14. A C++ binding with typed enums.
- **The windowing library's own `GetProcAddress`** with a hand-written function table.

This research read only release metadata for these, not their docs, so they are listed for completeness.

### Who creates the context

- **GLFW 3.5.1:**
  - **Context with the window:** GLFW creates the context together with the window, through WGL on Windows and GLX or EGL on X11, chosen by `GLFW_CONTEXT_CREATION_API` (`NATIVE`, `EGL` or `OSMESA`). On Wayland, EGL is the native API ([window guide](https://www.glfw.org/docs/latest/window_guide.html)).
  - **Mixing EGL and GLX** contexts in one process can segfault "on some Linux systems", so the docs advise picking one.
  - **Context hints:**
    - `GLFW_CONTEXT_VERSION_MAJOR/MINOR`: creation fails if the context's version is below the one requested.
    - `GLFW_OPENGL_PROFILE`
    - `GLFW_CONTEXT_DEBUG` (since 3.4; `GLFW_OPENGL_DEBUG_CONTEXT` is the old name)
    - `GLFW_CONTEXT_NO_ERROR` and `GLFW_CONTEXT_ROBUSTNESS`
  - **No windowless contexts:** "GLFW doesn't support creating contexts without an associated window", but a hidden window (`GLFW_VISIBLE` false) "can be used as a plain offscreen context" ([context guide](https://www.glfw.org/docs/latest/context_guide.html)).
  - **Threads:** a context is current on one thread at a time.
- **SDL 3** (release-3.4.18):
  - **Context attributes:** `SDL_GL_CONTEXT_MAJOR_VERSION`, `SDL_GL_CONTEXT_PROFILE_MASK` (`SDL_GL_CONTEXT_PROFILE_CORE`), `SDL_GL_CONTEXT_FLAGS` (`SDL_GL_CONTEXT_DEBUG_FLAG`) and `SDL_GL_EGL_PLATFORM` ([SDL_video.h L489–L521](https://github.com/libsdl-org/SDL/blob/release-3.4.18/include/SDL3/SDL_video.h#L489-L521)).
  - **Its "offscreen" video driver creates GL contexts through EGL** ([SDL_offscreenopengles.c L29–L62](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/video/offscreen/SDL_offscreenopengles.c#L29-L62)). It opens the display with `EGL_PLATFORM_DEVICE_EXT` ([SDL_egl.c L614–L661](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/video/SDL_egl.c#L614-L661)). That gives a windowless GL context on Linux, given an EGL with device-platform support such as Mesa's.
- **Creating the context directly with WGL and GLX or EGL:**
  - **WGL needs a throwaway context first.** A core 4.x context comes from `wglCreateContextAttribsARB` ([WGL_ARB_create_context](https://registry.khronos.org/OpenGL/extensions/ARB/WGL_ARB_create_context.txt)). It is a WGL extension that has to be fetched with `wglGetProcAddress`. That returns NULL "When no current rendering context exists", and extension addresses are "unique for each pixel format" ([wglGetProcAddress](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglgetprocaddress)). So the classic WGL path makes a dummy window and legacy context, fetches the function, and then makes the real context. GLFW and SDL already do this.
  - **On Linux,** `EGL_MESA_platform_surfaceless` gives a platform with no native surfaces, intended "for enabling OpenGL and OpenGL ES applications on systems where no window system exists" ([spec, Overview](https://registry.khronos.org/EGL/extensions/MESA/EGL_MESA_platform_surfaceless.txt)). Mesa's EGL supports the android, device, drm, surfaceless, wayland and x11 platforms, chosen with `EGL_PLATFORM` ([Mesa EGL docs](https://docs.mesa3d.org/egl.html)).
  - **The trade-off:** a backend that owns its own context code can make a headless context without a window, but it duplicates what the windowing library already does for the windowed case.

### A headless context for CI

- **There is no GPU:** standard GitHub-hosted runners (`windows-2025`, `windows-2022`, `ubuntu-24.04`, `ubuntu-26.04` and others) list none. GPU runners are a "larger runners" feature for GitHub Team and Enterprise Cloud plans ([GitHub-hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)).
- **Linux:**
  - **The renderer is Mesa llvmpipe,** forced with `LIBGL_ALWAYS_SOFTWARE=true` (and `GALLIUM_DRIVER=llvmpipe`) ([Mesa envvars](https://docs.mesa3d.org/envvars.html)).
  - **Getting a context:** either EGL surfaceless or device, as the backend's own code or SDL's offscreen driver, or GLX/EGL under a virtual X server with a hidden GLFW window.
  - **OSMesa is gone.** Mesa 25.1.0's changelog has "Remove osmesa" ([25.1.0 release notes](https://docs.mesa3d.org/relnotes/25.1.0.html)), so GLFW's `GLFW_OSMESA_CONTEXT_API` no longer works with current Mesa.
  - **The version on the runner depends on the image's Mesa.** Ubuntu 24.04's package gives llvmpipe **4.5** (report 17040); Mesa 26.2 gives 4.6 (report 17599). A 4.6 floor would need a newer Mesa on 24.04 runners (a PPA or a source build), or the `ubuntu-26.04` image if its Mesa reports 4.6. The 26.04 image was not checked.
- **Windows:**
  - **The system `opengl32.dll` cannot help.** Without a vendor ICD, it has no 4.x implementation to load (Microsoft's built-in software GL is legacy; this research did not re-read a Microsoft source for its exact version), so a runner without a GPU needs Mesa beside the executable.
  - **Mesa's Windows build produces a drop-in `opengl32.dll`** plus `libgallium_wgl.dll`, and the docs say to put "both DLLs in the same directory as your application" ([llvmpipe docs, "Windows"](https://docs.mesa3d.org/drivers/llvmpipe.html)).
  - **Gallium drivers on Windows:** llvmpipe, and d3d12, which on a GPU-less machine runs over WARP at GL 4.6 (report 17645, `D3D12 (Microsoft Basic Render Driver)`).
  - **No official binaries:** Mesa "does not provide precompiled libraries" ([Precompiled Libraries](https://docs.mesa3d.org/precompiled.html)). CI would build Mesa or use a third-party build. Third-party builds were not evaluated here.
- **Both platforms:**
  - **No vendor driver quirks.** A software renderer catches API misuse through `KHR_debug` but not vendor-driver bugs.
  - **Optional features need fallbacks under test.** Bindless is absent on llvmpipe and d3d12 (section 2), so CI would exercise only the fallback path of anything optional.

---

## Options and trade-offs (not a decision)

- **Version floor:**
  - **4.6 core:** SPIR-V, `gl_DrawID` and indirect count become core. It excludes nouveau, pre-GCN AMD on Windows, Fermi NVIDIA, and llvmpipe before Mesa 26.x (Ubuntu 24.04 CI).
  - **4.5 core:** DSA and clip control (the parts closest to an explicit API) work everywhere above, including Ubuntu 24.04's llvmpipe. 4.6 features become runtime checks.
- **Explicit-style subset with no fallbacks needed at a 4.5 floor:** DSA, immutable textures and buffers, persistent mapping with fences, separate programs and pipelines, sampler objects, `glClipControl(UPPER_LEFT, ZERO_TO_ONE)` and `KHR_debug`. All are in RenderDoc's supported list and on every current Mesa and proprietary driver.
  - **Multi-draw indirect** is core at 4.3; adding `gl_DrawID` and indirect counts needs 4.6 or their ARB extensions.
- **Bindless:** a separate decision. It is missing on Intel Linux, llvmpipe, d3d12 and under RenderDoc, so it is only workable as an optional fast path with a texture-unit fallback.
- **Shader ingestion:**
  - **SPIR-V via `glSpecializeShader`:** one compiled artifact shared with future Vulkan, D3D and Metal tooling. The costs: explicit locations and bindings everywhere, engine-side reflection (SPIRV-Cross), no Mesa disk cache, and a less-exercised driver path.
  - **GLSL source:** driver reflection and caching.
  - **SPIR-V compiled offline, then SPIRV-Cross to GLSL:** one source, with GL getting GLSL.
- **Context ownership:**
  - **The windowing library creates the context (GLFW or SDL):** it handles WGL's dummy-context dance, and headless means a hidden window or SDL's EGL offscreen driver.
  - **The backend creates it (WGL, GLX or EGL):** it can make a true windowless EGL context for tests, but duplicates the windowed path. The choice interacts with the map's ADR on who owns the context and swapchain.
- **Loader:**
  - **glad 2 with `mx`** gives a per-context function table that never reaches public headers, matching ADR-0010's `PRIVATE` linking.
  - **glad 2 with globals and `debug`** gives call interception for error checking.
  - The two options are mutually exclusive.
