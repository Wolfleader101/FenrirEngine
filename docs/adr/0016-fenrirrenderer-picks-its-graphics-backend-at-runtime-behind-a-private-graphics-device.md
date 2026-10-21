---
status: accepted
---

# FenrirRenderer picks its graphics backend at runtime behind a private graphics device

FenrirRenderer draws with OpenGL today, but D3D, Vulkan or Metal must be able to replace it without touching the code that calls the renderer. So the renderer has two layers: its public interface (components, meshes, materials, cameras), which game code uses, and below it a private, thin **graphics device** shaped like the explicit APIs, which each graphics backend implements. The backend is chosen at runtime, from OpenGL and a null backend.

## The seam

- **The graphics device is shaped like the explicit APIs.** Buffers, textures, samplers, pipelines, binding groups and passes, as the surveyed engines with a GL backend beside explicit ones do (Diligent, wgpu, sokol, Filament; branch `research/graphics-api-seams`). Passes, materials, debug drawing and later feature families are written once, above it. The OpenGL backend emulates the shape with OpenGL 4.6's (the floor since ADR-0017) direct state access, immutable storage, clip control, samplers and separate programs; barriers are tracked inside each backend, so the renderer never sees them.
- **Picked at runtime.** The device is an abstract interface. `RendererConfig` names the backend with an enum (`OpenGL`, `Null`, default `OpenGL`) passed to `Renderer::Install`. The null backend is always built: it creates handles and does the bookkeeping, draws nothing and needs no window or context, so tests and a headless app can pick it. If the requested backend cannot start (no GL 4.6 context), `Install` fails with a diagnostic and never falls back to null.
- **Commands are recorded, then submitted.** Renderer code records through a per-pass encoder into a `CommandList` that FenrirRenderer owns, holding plain-struct commands; it never draws on the device directly. Resources are created and destroyed through virtual calls, and one virtual `Submit` hands a list to the backend, which replays it. Recording needs no backend, so it may happen on any thread, and tests and sorting can read a list without a GPU.
- **Handles are typed, generational and owned by the device.** `BufferHandle`, `TextureHandle`, `PipelineHandle` and the rest are an index plus a generation, so stale use is caught without reference counts. Destruction is deferred by frame index until no frame in flight can use the object; GL needs no delay, but the hook is there for the explicit APIs.
- **The graphics device is private.** Its headers are not installed. Libraries stay hidden at compile level as ADR-0010 asks: only `src/Backend/OpenGL/` includes GL or its loader, and they link `PRIVATE`.
- **A UI library depends on nothing in Fenrir.** Glue adapts its draw data to a public overlay interface of FenrirRenderer, not to the graphics device. That interface must let a UI bring its own shaders and vertex layouts (an SDF UI needs them), not only textured triangles.

## What we refuse, accept and contain

- **A native escape hatch** (a GL id, the context): refused. A need for one gets a neutral API in FenrirRenderer.
- **Backend tuning settings** (GL version or profile hints, driver workarounds): refused. Public settings are API-neutral: vsync, MSAA, debug validation.
- **Behaviour differences between backends:** accepted where tests do not pin them. Conventions are not differences: the device fixes [0, 1] depth and an upper-left origin for every backend.
- **Threading:** contained. Every GPU call comes from one thread, as GL forces; which thread is the frame's decision.

## Considered options

- **Compile-time selection, as ADR-0010 chose for physics.** Rejected: the null backend is a second adapter, so the seam is real, and unlike Jolt a GPU is not on every machine that runs the tests.
- **Virtual calls for every command** instead of a recorded list. Rejected: a little less code, but every command would run on the GPU thread, and tests would need a spy backend to see what was drawn. Because commands go through the encoder, the choice stays inside FenrirRenderer either way.
- **A thin seam alone, public**, which UI libraries or game code record into. Rejected: its handle and pass rules would become everyone's, and a UI library would depend on Fenrir.
- **A thin seam shaped like GL** (bgfx), explicit backends hashing loose state into pipelines at draw time. Rejected: the cost lands on the backends we want next.
- **One high-level renderer per graphics API** (Godot's Compatibility renderer). Rejected: every feature family would be written once per API.

## When to revisit

If profiling or a render thread shows the command list's cost, or a second package truly needs raw GPU work through the graphics device.
