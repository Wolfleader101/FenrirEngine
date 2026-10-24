# How do other engines put a seam over graphics APIs?

Type: research
Status: resolved
Blocked by: none

## Question

How do engines and libraries that support several graphics APIs draw the line between the code that renders and the API underneath, in the areas where that constrains FenrirRenderer's seam? Read bgfx, sokol_gfx, Diligent Engine, NVRHI, wgpu (and Bevy's use of it), SDL3's GPU API, Godot's `RenderingDevice`, Unreal's RHI, O3DE Atom's RHI and Filament's backend:

- **Altitude:** a thin interface over GPU resources and commands (buffers, textures, pipelines, command lists) or a higher-level renderer with one implementation per API. What sits above and below the line in each.
- **Selection:** backend picked at compile time (build option, typedefs) or at runtime (abstract interface, function tables), and what each costs.
- **OpenGL beside explicit APIs:** how GL's global state machine and implicit synchronisation are fitted under an interface shaped for Vulkan, D3D12 or Metal (pipeline state objects, descriptor sets or bind groups, render passes, barriers), and what each design gives up for GL.
- **Handles and lifetimes:** opaque handles, generation counters, deferred destruction across frames in flight.
- **Leaks:** native escape hatches, backend-specific features and limits, shader formats, threading shaped by the backend; which leak and why.
- **Testing:** null or headless backends, and how they test without a GPU.

Write findings to a Markdown file on the `research/graphics-api-seams` branch (`.scratch/fenrir-renderer/research/graphics-api-seams.md`), citing each claim's source and pinning the versions read, and link it here.

## Answer

Findings: `.scratch/fenrir-renderer/research/graphics-api-seams.md` on branch `research/graphics-api-seams` (commit `3a9c047`), against bgfx `21a5cf8`, sokol `efeb776`, DiligentCore `4678e43`, NVRHI `6b96fb0`, wgpu `3050396`, Bevy `2614884`, SDL `829a65d`, Godot `6790667`, O3DE `2462b90`, Filament `a843093` and the UE 5.8 docs, each claim cited. Gaps: Unreal's OpenGL backend docs unreachable; NVRHI's tests described from its repo only. It ends with seam options and trade-offs for "Where does the graphics seam go, and is the backend picked at compile time or at runtime?".

- **Altitude:** most put a thin, explicit-API-shaped seam right over the API (NVRHI, Diligent, SDL3 GPU, sokol_gfx, wgpu-hal, Godot's `RenderingDeviceDriver`, O3DE Atom, Unreal's `FDynamicRHI`); several add a higher layer over it, a safe tracking layer (wgpu-core, Godot's `RenderingDevice`) or a renderer with private backends (Filament, Bevy over wgpu). bgfx sits higher: backends take a whole recorded frame.
- **Selection:** only sokol_gfx is purely compile-time (one `#define` per build); wgpu-hal dispatches statically but wgpu-core holds a `Box<dyn DynDevice>`; the rest choose at runtime through interfaces or function tables, keeping crossings coarse (a frame per call in bgfx, structure-filling draws in NVRHI, a command stream replayed on a driver thread in Filament).
- **GL beside explicit APIs:** NVRHI, SDL3 GPU and O3DE dropped GL. Diligent, wgpu, sokol and Filament emulate the explicit model on GL: pipelines as cached GL state, descriptor sets flattened to binding points, passes as framebuffer binds and clears, barriers as no-ops but `glMemoryBarrier`. GL loses multithreaded recording; wgpu ranks it a partial "secondary" backend.
- **Two other fits for GL:** bgfx exposes GL-like loose state flags and its Vulkan backend hashes them into a pipeline cache at draw time; Godot keeps GL out of its thin seam, its Compatibility renderer implementing the high-level `RendererCompositor` directly with different shaders.
- **Handles:** a bare 16-bit index (bgfx), index plus generation (sokol, Filament's 4-bit age catching use-after-free), opaque 64-bit ids backed by pointers (Godot's driver, SDL), ref-counted objects (NVRHI, Diligent, Unreal, wgpu's `Arc`).
- **Deferred destruction** everywhere, keyed on the frame (bgfx, sokol, Godot, O3DE) or on the submission's fence (Diligent, NVRHI, wgpu-core, SDL); SDL's cycling and NVRHI's constant-buffer versioning rotate copies to avoid reuse hazards.
- **Native escape hatches:** every library has one except SDL3 GPU (creation properties only), for interop, adopting an existing device or context, and features the neutral API lacks.
- **Other leaks:** feature and limit queries (wgpu, NVRHI, Godot), API conventions (SDL fixes D3D-style coordinates; Godot's enums follow Vulkan), backend tuning settings (Bevy's `dx12_shader_compiler`, `gles3_minor_version`).
- **Shader formats:** no shared binary anywhere; per-API formats compiled offline (SDL, NVRHI, bgfx's shaderc, sokol-shdc), one source translated (wgpu through naga, Diligent HLSL to GLSL or SPIR-V, Godot SPIR-V per driver), or per-API variants baked into assets (O3DE, Filament).
- **Threading:** GL's thread-bound context forces one render or driver thread (bgfx, Filament, Diligent's GL backend); parallel recording only where the backend allows (Diligent deferred contexts, NVRHI command lists, SDL per-thread command buffers, Unreal's RHI thread).
- **Testing:** null backends test bookkeeping without a GPU (wgpu `noop` used by Bevy's tests, sokol dummy, bgfx Noop, Filament NOOP, O3DE Null RHI, Unreal `FNullDynamicRHI`, Godot `RasterizerDummy`); pixels are tested by image comparison against golden or reference images on real or software adapters (Diligent `--mode=*_sw`, wgpu on llvmpipe, Filament's golden PNGs).
