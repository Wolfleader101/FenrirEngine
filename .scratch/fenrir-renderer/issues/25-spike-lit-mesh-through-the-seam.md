# Spike: a lit mesh, a camera, a skybox and debug lines through the proposed seam

Type: prototype
Status: resolved
Blocked by: 08, 11, 12, 13, 14

## Question

Build a throwaway spike on a `prototype/fenrir-renderer-spike` branch that draws a lit mesh, a skybox and debug lines from a camera entity, through the proposed graphics seam, FenrirWindow and the ECS model, to check the decisions before the foundation spec is written.

- Does the backend stay hidden (no GL or windowing header reaches callers)?
- Does the seam's shape survive real GL code, and what would a second backend have to fight?
- Lessons for the spec, as the physics spikes recorded.

## Answer

**Verdict: the seam, FenrirWindow and the ECS model work together through real GL code; neither GL nor SDL leaks.** The user ran the spike app and verified it by eye: lit metallic-roughness spheres, a textured mipmapped ground, a spinning parent with an orbiting child, an unlit cube, the magenta fallback, an alpha-blended sphere, a cubemap sky, debug lines and a second picture-in-picture camera, with a fly camera, vsync switching, resizing, minimising and closing.

Prototype (primary source, never merged): branch `prototype/fenrir-renderer-spike` (`aac9367`), based on `dotnet-scripting`. The thin slice: SDL3 3.4.18 behind FenrirWindow, a minimal FenrirInput fed by it, FenrirAssets (handles, `Add`, revisions, procedural meshes), FenrirDebugDraw (lines), `FenrirShaderCompiler` (glslang and SPIRV-Reflect) writing one container per program embedded as a byte array, the private graphics device with OpenGL 4.6 and null backends, a cut-down render graph, the renderer core and its ECS facade, and `examples/RendererSpike`. `FENRIR_SPIKE_BACKEND=null` and `FENRIR_SPIKE_FRAMES=N` make it a headless check.

**Evidence:**

- **The backend stays hidden.** By `compile_commands.json`, the GL loader's include path reaches only `GLDevice.cpp` and `glad.c`, SDL's only `Window.cpp`, glslang's only the build-time tool; no public header or app file names GL, SDL, the device or the core. The core is its own target with no FenrirApp, FenrirECS or FenrirScene dependency; the GL backend is a target of its own under it.
- **The seam's shape survives GL.** Pipelines, binding-group layouts and groups, per-pass encoders recording into plain-data command lists, typed generational handles and frame-deferred destruction all map onto DSA, immutable storage, samplers, `glBindBufferRange` and `glBindTextureUnit`, with nothing leaking upward. One linked program per vertex and fragment pair sufficed; separable programs were not needed.
- **Both backends:** GL (NVIDIA RTX 5080, driver 617.14) and null give the same frame: 16 items, 37 draws, 9 passes, the 2 unused passes culled, in stage order. No GL debug error. Clang x64 Debug and Release build and run. **Not verified:** MSVC, Linux, AMD, Intel, Mesa.

**What a second backend would fight:** not the device's shape. The SPIR-V a Vulkan backend wants is what the compiler makes before its GL rewrite (lesson 1); the declared barriers, queue kinds and per-pass lists are in place but unexercised; the clip-origin rule (lesson 3) is GL's alone. Its real costs are what GL let the spike skip: memory allocation, a swapchain behind `GraphicsSurface`, and descriptor pools behind binding groups.

**Lessons for the foundation spec:**

1. **glslang takes `layout(set = N)` only with Vulkan semantics.** `FenrirShaderCompiler` compiles every stage as Vulkan GLSL to SPIR-V 1.0, then rewrites a GL copy: sets zeroed, bindings flattened, `VertexIndex` retargeted to GL's `VertexId` (they agree, base vertex included). `gl_InstanceIndex` has no GL equivalent (GL's leaves out the base instance) and is refused; shaders index draws by `gl_BaseInstance`, which both accept. GL 4.6 took the rewritten SPIR-V unchanged. ADR-0018's one language holds, with the Vulkan dialect as that language.
2. **Flattening is a formula shared by the tool and the GL backend, per resource kind:** GL binding = set x 4 + binding, separately for uniform buffers, storage buffers and textures. That keeps four sets of textures inside GL's minimum of 16 fragment texture units (set x 8 would overflow it), so at most 4 bindings of a kind per set; ADR-0027's 4 texture slots fit.
3. **GL's clip origin is set per pass.** Upper-left for render targets stores them top row first, as uploaded images and the explicit APIs do, so materials and the output pass sample with one convention; the window's row 0 is its bottom, so a pass drawing to it switches to lower-left. GL compensates winding itself: front faces stay counter-clockwise in both (the spike's first guess, clockwise, culled the ground plane). NDC +y is up, as in D3D and Metal; a Vulkan backend flips its viewport.
4. **ADR-0024's "the shader's GLSL default" does not exist:** std140 block members cannot carry initialisers, and reflection carries offsets, not values. The spike seeded the built-ins' defaults in C++ (base colour white, roughness 0.5) and zeroed the rest. Where user shaders' defaults come from goes to ticket 41.
5. **FenrirApp needs app-wide resources** (ADR-0017's `Keyboard`, ADR-0021's asset store, the renderer's and window's state). The spike added `GetResource` and `AddResource` with Scene's shape; the owners (`Window`, `Renderer`) hold the `App` and reach their state through it, so the owner values can move. Window's surface functions are statics taking `App&` for the same reason.
6. **Main-thread systems must be sequential:** `App::AddSystem` runs on a worker; FenrirWindow's event pump and the `Render` phase are added with `AddSequentialSystems`. The spec states it for every package that touches SDL or the GPU (or the scheduler gains a main-thread rule).
7. **`Install` needs a failure shape.** ADR-0016 says `Install` fails with a diagnostic; the spike returns an owner whose `IsValid()` is false, after logging. The spec picks one shape for every package's `Install`.
8. **The first frame hitches** (about 0.18 s in Debug, beyond the 5-tick cap, logged by FenrirApp) as pipelines are made, as ADR-0024 expects; the foundation can prepare known materials at load.
9. **Reading the backbuffer back** needs the front buffer after `Present` (or a read before it). Ticket 20's GL tests read offscreen targets, so this touches only screenshots.

**Not spiked, still risk:** MSAA and its resolve, several windows, resizing while the event pump blocks (already in *Not yet specified*), text, the overlay, and C#.

**Graduated:** ticket 41 (where material parameter defaults come from). No ADR is needed; lessons 1 to 3 refine ADR-0018 and ADR-0016 within their decisions, and lesson 4 is ticket 41's to settle as an amendment to ADR-0024.
