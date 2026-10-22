# 09: The renderer core, the graphics device and the null backend

**What to build:** FenrirRenderer's lower two layers exist and are tested with no `App` and no GPU: the private graphics device with its always-built null backend, `CommandList` recording, and a render graph compiled every frame (spec: "FenrirRenderer: layers and the graphics device" (three layers, `GraphicsSurface`, device shape, null backend), "FenrirRenderer: the frame" (render graph), "Testing Decisions" seam 1; ADR-0016, ADR-0019). The ECS facade and the first draws are ticket 11; the GL backend ticket 10.

- **Targets:** the renderer core as its own CMake target (headers not installed), with no FenrirApp, FenrirECS, FenrirScene or FenrirWindow dependency, under `FENRIR_ENABLE_RENDERER`. It may use FenrirAssets' data types and the shader container reader, never the store.
- **`GraphicsSurface`:** a private abstract interface declared by the core and used by backends; tests use a fake surface.
- **The device:** a private abstract interface in the explicit APIs' shape: buffers, textures, samplers, pipelines, binding-group layouts and binding groups, passes. Resources are created and destroyed through virtual calls. Handles (`BufferHandle`, `TextureHandle`, `PipelineHandle`…) are typed index-plus-generation values owned by the device; destruction is deferred by frame index, and a stale handle is detected. Barriers are derived inside each backend from declared pass usage.
- **`CommandList`:** a plain-struct list the renderer owns, recorded through per-pass encoders with no backend, replayed by one virtual `Submit`.
- **The null backend** in `src/Backend/Null/`, always built: no window or context, creates handles, does all bookkeeping, draws nothing, accepts any shader container.
- **The render graph:** passes declare resources with precise usage (colour attachment, depth read or write, sampled, storage, copy source or destination), a queue kind (`Graphics`, `Compute`, `Copy`) and a record function into their own `CommandList`. Order by render stage (`Shadow, Opaque, Background, Transparent, Debug, PostProcess, Output, Overlay`), then registration order; a pass reading another pass's output is ordered after it. Passes whose outputs nothing reads are culled; a pass reading what nothing wrote is an error. Transients from a pool keyed by description with computed lifetimes; external resources (backbuffer, image targets) imported. Per-frame buffers ring by frame index (`framesInFlight`, default 2).
- **Render features** live in the core and add passes each frame; the interface is internal (game code and other packages add none).
- **`RendererProbe`:** a friend struct in the renderer's tests, as `PhysicsProbe` is, reading the `CommandList`s the null backend received and the device's live handles.
- The graph's "designed for, not built" list (ADR-0019) noted in the code where each would go.

**Blocked by:** 06 (FenrirShaderCompiler and the shader container).

**Touches:** `packages/FenrirRenderer/`

**Status:** resolved

- [x] Core tests with a fake surface and no `App`: resources are created, used and destroyed; a destroyed handle's slot is freed only after `framesInFlight` frames, and its old handle is detected as stale.
- [x] A recorded `CommandList` replays through the null backend's `Submit`, and `RendererProbe` reads back the passes, pipelines, bindings and draws in order.
- [x] Graph: passes order by stage then registration; a reader is ordered after its writer across stages; an unread pass is culled; reading an unwritten resource is reported as an error; two transients with equal descriptions and disjoint lifetimes share a pool entry; an imported resource is never pooled.
- [x] Per-frame ring buffers advance with the frame index.
- [x] No GL or loader include is reachable from the core or the null backend; Clang and MSVC x64 debug and release presets build with no new warnings.
