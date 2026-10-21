# How engines build and submit a frame

Research for [05-how-engines-build-a-frame](../issues/05-how-engines-build-a-frame.md), on the [FenrirRenderer map](../map.md).

This file reports what engines do and lists options with their trade-offs. It does not pick one.

## Sources and versions

FenrirEngine paths are at commit `cf09b0f`. Source was read at these tags or commits:

- Bevy `v0.20.0`
- Godot `4.7.2-stable`
- Filament `v1.78.0`
- The Forge `v1.63`
- bgfx `master` at `21a5cf8f` (2026-10-21); bgfx has no release tags
- raylib `6.2`
- im3d `master` at `3fd7afaf`
- Dear ImGui `v1.93.0`
- RmlUi `6.3`
- msdf-atlas-gen `v1.4`
- stb `master` at `a7d4c217` (`stb_truetype.h` v1.26)

Docs were read at these versions:

- Unreal Engine 5.8 docs (dev.epicgames.com). Unreal source is behind an Epic account, so Unreal claims come from the docs only.
- Unity 6.2 docs (6000.2); `RenderingThreadingMode` at 6.6.
- Godot docs 4.5.
- GLFW 3.5.1 docs; OpenGL 4.6 Core spec (May 5, 2022); the Vulkan spec ("latest" on docs.vulkan.org, read 2026-10-21); Microsoft Learn for WGL and D3D12.
- Frostbite: Yuriy O'Donnell, "FrameGraph: Extensible Rendering Architecture in Frostbite", GDC 2017. Slides are on SlideShare (DICEStudio upload), and the GDC Vault page has the abstract.

Paths in the engine sections are relative to that engine's repo root. Anything marked **(inference)** is my reading, not something a source states.

---

## 0. FenrirEngine today

What the frame design has to fit:

- **Phases.** `SchedulePriority` has `PreInit, Init, PostInit, PreUpdate, Tick, Update, PostUpdate, Exit` (`packages/FenrirScheduler/include/FenrirScheduler/Scheduler.hpp` L21-33). No phase is named for rendering.
- **Loop.** `App::Run` runs `PreUpdate`, then `Tick` k times (`while accumulator >= tickRate`), then `Update` and `PostUpdate`, then rotates the event queues (`packages/FenrirApp/src/App.cpp` L46-72). `tickRate` defaults to 1/60 s, and the remainder stays in `Time::accumulator` (`packages/FenrirTime/include/FenrirTime/Time.hpp` L12-14). So `accumulator / tickRate` after the tick loop is the interpolation fraction a renderer would need **(inference)**.
- **Threads.** In a phase, `AddSystems` systems are sent to a `ThreadPool` and awaited. Only then do `AddSequentialSystems` systems run, in order, on the calling thread (`packages/FenrirScheduler/src/Scheduler.cpp` L18-47).
  - A GL context is current on one thread at a time (§3), so a system that calls GL must be a sequential system on the thread that owns the context. A pool thread would have no context **(inference)**.
- **Packages.** `packages/FenrirRenderer` and `packages/FenrirWindow` contain only a `CMakeLists.txt`.

---

## 1. Passes: a render graph or fixed passes

### What the engines do

**Frostbite FrameGraph (GDC 2017)**

- **Before the graph**, the renderer was immediate-mode with manual resource management and hand-tuned ESRAM handling. It grew from about 4k to 15k lines, with single functions over 2k lines, and was "expensive to maintain, extend and merge" (slides 9, 11).
- **Three phases:**
  - *Setup*: each pass declares the resources it creates, reads and writes.
  - *Compile*: unreferenced passes and resources are culled, lifetimes are computed, and transient memory is allocated greedily (acquired before first use, released after last use).
  - *Execute*: per-pass lambdas run against the usual render context, with handles resolved to real resources (slides 19-29).
- **Transient resources** live at most one frame and are aliased in memory. At 720p the frame needed 147 MB without aliasing and 76-80 MB with it. At 4K on DX12 it was 1042 MB against 472 MB (slides 51-58).
- **Aliasing is "harder than it sounds"** (slide 45): metadata, aliasing barriers and async-compute hazards all need handling (slides 45-50).
- **Async compute** is opt-in per pass (slides 30-33).
- **Result**: render module code went from about 15k to 5k lines (slide 36).

**Unreal RDG (UE 5.8 docs, "Render Dependency Graph")**

- Passes are recorded into a graph during a setup timeline and run later; each pass declares its resources through a parameter struct.
- RDG handles transitions itself, using split barriers. It culls unused passes and resources, aliases transient resources with disjoint lifetimes, schedules async compute by inserting fences, and runs pass lambdas in parallel.
- The docs say "All high-level rendering code should be written using RDG."
- It has an Immediate Mode for debugging and a validation layer that adds CPU cost (compiled out of Test and Shipping builds).

**Unity URP render graph (Unity 6.2, "Introduction to the render graph system")**

- Passes declare their textures while recording and issue commands in an execution stage.
- The graph removes passes whose output the frame does not use. It allocates memory before the first writer, frees it after the last reader, and reuses memory between matching textures.
- On tile-based mobile GPUs it merges passes into native render passes that keep data in tile memory.
- Limits: you cannot allocate or dispose of graph resources yourself, and imported resources are not lifetime-managed by the graph.

**Filament `v1.78.0`, the frame graph**

- `FrameGraph::addPass<Data>(name, setup, execute)` takes a setup lambda that declares resources synchronously, and an execute lambda that is "called asynchronously from FrameGraph::execute()" and must capture by copy (`filament/src/fg/FrameGraph.h` L266-297).
- `compile()` "Allocates concrete resources and culls unreferenced passes" (L317-320). `sideEffect()` keeps a pass from being culled (L170-172). Resources can be imported (L388-420).
- Barriers are not the graph's job: the graph emits backend `DriverApi` calls and the backend handles synchronisation **(inference from the API)**.

**Godot `4.7.2`: fixed passes over an automatic command graph**

- The Forward+ renderer's passes are fixed, in code order, in `RenderForwardClustered::_render_scene`: setup, sky setup, depth pre-pass, opaque pass, motion vectors, sky, MSAA resolve, subsurface scattering, transparent pass, resolve, upscaling (`servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` L1704-2471, labelled by `RENDER_TIMESTAMP` and `draw_command_begin_label`).
- Under that, `RenderingDeviceGraph` records every command with the resources it uses and builds dependencies between commands. When `p_reorder_commands` is set, `end()` topologically sorts the commands, groups them by type, and inserts the barriers itself (`servers/rendering/rendering_device_graph.cpp` L2583-2650; `servers/rendering/rendering_device_graph.h` L777-839, L953).
- So Godot gets automatic barriers and reordering without passes declaring anything: the graph is inferred from the recorded commands. It does not alias transient memory **(inference: no aliasing code in the graph)**.

**Bevy `v0.20.0`: the "render graph" is now ECS schedules**

- The root `RenderGraph` is a schedule. Its `camera_driver` system runs each camera's schedule (`Core2d`, `Core3d` or a custom one) (`crates/bevy_render/src/renderer/mod.rs` L32-48; `crates/bevy_core_pipeline/src/schedule.rs` L1-11, L128-135).
- `Core3d` is a chain of system sets: `Prepass, MainPass, EarlyPostProcess, PostProcess` (`crates/bevy_core_pipeline/src/schedule.rs` L36-69).
- Passes are ordinary systems ordered with `.after()`/`.before()`. For example, the UI pass is `ui_pass.after(Core3dSystems::PostProcess).before(upscaling)` (`crates/bevy_ui_render/src/lib.rs` L297-302).
- Barriers and memory come from wgpu, not from a Bevy graph **(inference: wgpu tracks resource state)**. There is no transient aliasing.

**The Forge `v1.63`: fixed passes, manual barriers**

- There is no render graph in `Common_3` (no graph source in the tree).
- Samples write each frame by hand:
  - acquire the swapchain image;
  - wait on a fence when the CPU is `gDataBufferCount` frames ahead;
  - `cmdResourceBarrier` to a render target;
  - bind targets, draw;
  - rebind the swapchain with `LOAD_ACTION_LOAD` and no depth, then draw UI;
  - barrier to present, submit, present.
- Source: `Examples_3/Unit_Tests/src/01_Transformations/01_Transformations.cpp` L903-1075; the barrier API is in `Common_3/Graphics/Interfaces/IGraphics.h` L2899.

**bgfx `21a5cf8f`: fixed "views" (small engine, fixed passes)**

- The user declares views up front, each with a render target, a clear, transforms and a view mode, then submits draws in any order.
- Each draw's 64-bit sort key puts the view id in the top bits, so draws are grouped by view first. Views are passes, ordered by id (`docs/internals.rst` L4-27).
- There is no graph, culling or aliasing. bgfx handles API state per backend.

**raylib `6.2`**

- `rlgl` is "a pseudo-OpenGL 1.1 immediate-mode style API". It accumulates vertices into a default batch and flushes it whenever a state change needs it (`src/rlgl.h` L3-19).
- Passes are whatever order the app calls things in between `BeginDrawing`/`EndDrawing`.

### What a graph buys, and when

From the sources above:

- **Automatic barriers and layout transitions.** This matters on Vulkan and D3D12, where the app owns them. It does not matter on OpenGL, where the driver tracks hazards **(inference)**. Godot shows you can get this from a command-level graph without declaring passes (Godot `rendering_device_graph.cpp` above).
- **Transient memory aliasing.** This is large at high resolution with many intermediate targets (Frostbite: about 46-55% saved). It is small for a frame with a few targets **(inference)**. OpenGL has no explicit memory aliasing to exploit **(inference)**.
- **Culling unused passes**, and decoupling features from each other. Frostbite cut its code from 15k to 5k lines. URP drops passes whose output is unused.
- **Async compute and parallel recording.** These are Vulkan, D3D12 and console features (Unreal RDG, Frostbite).
- **Cost**: two-phase lambdas and handles instead of direct calls, validation work, and debugging through indirection (Unreal's Immediate Mode exists for this).

### Options for FenrirRenderer

| Option | What it is | For | Against |
|---|---|---|---|
| A. Fixed passes in code | A fixed list (shadow, opaque, sky, transparent, debug, UI) in one function, like The Forge samples or Godot's `_render_scene`. | Simplest; easy to read and debug; fits GL, which needs no barriers. | Adding post-processing means editing the list; a Vulkan or D3D12 backend would have to own barriers below the seam. |
| B. Ordered pass list (bgfx-style views) | Passes are ids or entries with a target and a clear; draws are tagged with a pass id and sorted. | Lets other packages add passes by inserting ids; no graph machinery. | No resource lifetimes, culling or barriers. |
| C. Passes as scheduler systems (Bevy-style) | Each pass is a sequential system in a render phase, ordered by registration or by before/after. | Reuses the scheduler. | `Scheduler` has no before/after ordering today (it only has a sequence per phase), and there is no render phase. |
| D. Declared render graph (Frostbite, RDG, Filament, URP) | Passes declare reads and writes; compile culls, allocates and inserts barriers. | Ready for Vulkan and D3D12 barriers and aliasing; post-processing composes. | The most code; little payoff on GL alone; can be added later behind A or B if the seam hides command recording. |
| E. Barriers inferred inside the backend (Godot `RenderingDeviceGraph`) | Fixed passes above; a future Vulkan or D3D12 backend tracks usage per command and inserts barriers. | Pass code stays simple; barriers stay below the seam. | That backend becomes a large component in its own right. |

---

## 2. Getting data out of the ECS

### What the engines do

**Bevy: a separate render world, synced and extracted**

- Rendering runs in a `SubApp` with its own `World` (`crates/bevy_render/src/lib.rs` L151-158).
- **Sync.** Each frame starts with a sync step. For entities that carry `SyncToSubWorld`, it spawns or despawns matching render-world entities and keeps a two-way link (`MainEntity`/`SubEntity`). It copies no component data (`crates/bevy_extract/src/sync_world.rs` L20-79).
- **Extract.** The main world is swapped into the render world as a `MainWorld` resource, with `mem::replace` and a scratch world to avoid allocation. Then `ExtractSchedule` runs systems that read main-world data through `Extract<…>` params (`crates/bevy_extract/src/extract_plugin.rs` L121-147).
- The docs say extraction "should be kept as short as possible" to leave room for pipelining (L100-108). The simple path is `ExtractComponent`, and `ExtractInstance` is the faster one (`crates/bevy_extract/src/lib.rs` L16-37).
- **After extraction**, the render schedule runs fixed sets in order: `ExtractCommands, PrepareMeshes, CreateViews, Specialize, PrepareViews, Queue, PhaseSort, Prepare, Render, Cleanup, PostCleanup` (`crates/bevy_render/src/lib.rs` L182-238, L317-370).
- **Interpolation is not built in.** Bevy documents a slot for it: `RunFixedMainLoopSystems::AfterFixedMainLoop`, "a system that interpolates the transform of an entity between the last and current fixed update" (`crates/bevy_app/src/main_schedule.rs` L470-490).
  - Its fixed-timestep example keeps the physics position apart from the visual `Transform` and interpolates between the previous and current physics positions, noting that the visual "is now slightly lagging behind" (`examples/movement/physics_in_fixed_timestep.rs` L40-53).
  - Extraction then copies whatever `Transform`/`GlobalTransform` holds at frame end **(inference)**.

**Unreal: render proxies (UE 5.8 docs)**

- `UPrimitiveComponent` lives on the game thread, and `FPrimitiveSceneProxy` is its counterpart on the render thread.
- The game thread must not touch the proxy once it is registered, and the render thread must not touch the component. Values the renderer needs are mirrored into the proxy and updated through enqueued render commands ("Threaded Rendering").
- Proxies emit `FMeshBatch`es, through `DrawStaticElements` (cached when the proxy is added) or `GetDynamicMeshElements` (each frame) ("Mesh Drawing Pipeline").

**Godot: a retained server with RIDs**

- Scene nodes push state into the `RenderingServer` (instances named by RID, with transforms set through server calls).
- When the server runs threaded, calls from other threads are queued into a `CommandQueueMT` (`servers/rendering/rendering_server_default.h` L80-91, L117-119).
- **3D physics interpolation** has moved to the scene side. `SceneTreeFTI` keeps previous and current transform lists and has a step that "Calculate[s] interpolated xforms, send[s] to visual server" (`scene/main/scene_tree_fti.h` L82-91, L158). The server's 3D `InterpolationData` now holds only an enabled flag (`servers/rendering/renderer_scene_cull.h` L1410-1418).
- Multimesh and canvas items still have server-side interpolation calls (`rendering_server_default.h` L454-458, L1073-1075).
- The docs describe the cost: the rendered state sits "between 1 and 2 ticks into the past" (Godot 4.5 docs, "Introduction to physics interpolation").

**Filament: its own components, pushed by the app**

- Filament keeps its own entity components (`TransformManager`, `RenderableManager`). The app copies transforms in with `setTransform(instance, mat4)` (`filament/include/filament/TransformManager.h` L41-68, L323-335).
- Instances are transient handles, invalidated by `create`/`destroy`/`setParent` and by `Renderer::endFrame` (L70-81).

**The Forge and bgfx: no scene layer**

- The app records draws straight from its own data. In bgfx, `submit` calls go into a frame buffer that the render thread consumes (`docs/internals.rst` L50-62).

### Options for FenrirRenderer

| Option | What it is | For | Against |
|---|---|---|---|
| A. Draw straight from components | Render systems query `MeshRenderer` + world transform and issue draws in the same frame. | Least code; no copies; fits a single thread. | Rendering cannot overlap the next frame's simulation; renderer data such as GPU handles and sort keys has to live on game entities or in side tables. |
| B. Extract into a render-side snapshot (Bevy-lite) | At frame end, copy the render-relevant components (transform, mesh, material, camera, lights, debug lines) into flat arrays owned by the renderer, then draw from those. | A clear seam; the snapshot is plain data; enables a render thread later; physics and other packages still produce plain data. | A copy per frame; needs a sync step for spawned and despawned entities, or a full rebuild each frame. |
| C. Persistent proxies (Unreal or Godot style) | Each renderable has a renderer-side object; components push changes (dirty flags or commands). | Static objects cost nothing per frame; cached draw commands (Unreal) are possible. | Lifecycle and synchronisation code; double bookkeeping. |

**Interpolated transforms.** Fenrir's `Tick` is a fixed step, so the choices are:

1. A system after the tick loop (in `Update` or `PostUpdate`) writes an interpolated transform from previous and current tick transforms with `alpha = accumulator / tickRate` (Bevy's documented slot; Godot's `SceneTreeFTI`).
2. The renderer or extraction step interpolates as it copies, given both transforms and alpha.

Either way, the shown state lags up to one tick (Godot docs: 1-2 ticks). The camera usually runs at frame rate (Bevy example, L54-58).

---

## 3. Threads

### What the engines do

- **Bevy.** By default, rendering runs between main-schedule runs. `PipelinedRenderingPlugin` moves the render app to its own thread, so frame N renders while frame N+1 simulates. Sync and extract run on the main thread; then the render thread applies extract commands and renders while the main schedule runs (`crates/bevy_render/src/pipelined_rendering.rs` L99-138).
- **Unreal (UE 5.8 docs).**
  - The render thread runs "a frame or two behind the game thread". The game thread blocks at the end of `Tick` if it gets further ahead. `FRenderCommandFence` and `FlushRenderingCommands` are the sync points ("Threaded Rendering").
  - An RHI thread translates platform-agnostic command lists into API calls. Work generated in parallel on the front end is translated in parallel on DX12 and Vulkan ("Parallel Rendering Overview").
  - Mesh draw command setup runs as one task per pass, and draw recording runs on several tasks ("Mesh Drawing Pipeline", "Mesh Drawing Parallelism").
- **Unity (`RenderingThreadingMode`, 6.6 docs).** Seven modes, from `Direct` (render from the main thread) through `MultiThreaded` (main thread makes intermediate commands, a render thread makes API calls) to the native graphics-jobs modes, where worker threads build or translate commands.
- **Godot.**
  - The `RenderingServer` can run on its own thread: a `WorkerThreadPool` task loops on `command_queue.flush_all()` after making the GL context current on that thread (`servers/rendering/rendering_server_default.cpp` L280-283, L415-424).
  - Calls from other threads are pushed to the queue (`rendering_server_default.h` L117-119). `draw()` must be called from the main thread (`rendering_server_default.cpp` L443-444).
- **Filament.** `Engine::create` starts a driver thread that creates the context and driver (`filament/src/details/Engine.cpp` L184-206). The `Renderer` "generates drawing commands for the render thread and manages frame latency" (`filament/include/filament/Renderer.h` L46-52). Frame-graph execute lambdas run asynchronously (`FrameGraph.h` L290-292).
- **bgfx.**
  - API thread and render thread, with two `Frame` buffers swapped at `bgfx::frame()` (`docs/internals.rst` L31-84).
  - Up to 8 encoders let several threads submit draws without contention (L95-104).
  - Resource calls are mutex-guarded from any thread, and the GPU work is deferred to the render thread (L66-84).
  - If `renderFrame` and `init` are called on the same thread, bgfx runs single-threaded (L70-80).
- **The Forge.** The sample records on one thread and keeps `gDataBufferCount = 2` frames in flight, waiting on a fence when the CPU gets too far ahead (`01_Transformations.cpp` L85, L917-921). `cmdDrawUserInterface` "must be made on the main thread" because ImGui is not thread-safe (`Common_3/Application/Interfaces/IUI.h` L507-513).

### What the APIs allow

- **OpenGL / WGL / GLFW.**
  - "A thread can have one current rendering context", and "A rendering context can be current to only one thread at a time" (Microsoft Learn, `wglMakeCurrent`, Remarks). GLFW says the same and requires making a context non-current before moving it (GLFW 3.5.1, "Context guide").
  - Shared contexts share buffers, programs and shaders, renderbuffers, samplers, sync objects and textures. Container objects (framebuffers, VAOs, program pipelines, transform feedback) "are not shared" (GL 4.6 Core §5).
  - Changes made in another context are not propagated automatically; the object must be re-bound to guarantee new contents are visible (GL 4.6 §5.3, Rules 3-4).
  - So in practice GL work is issued from one thread. Other threads can prepare data, such as building vertex arrays or writing into memory already mapped by the GL thread **(inference)**.
- **Vulkan.** "Vulkan is intended to provide scalable performance when used on multiple host threads." Command pools and the command buffers allocated from them, and the queue in `vkQueueSubmit`, are externally synchronised (Vulkan spec, "Threading Behavior"). So it is one command pool per recording thread, with submission serialised.
- **D3D12.** An allocator can back only one recording command list at a time and is "not free-threaded". Microsoft suggests about `numRecordingThreads * MaxFrameLatency` allocators (Microsoft Learn, "Creating and recording command lists and bundles").

### Options for FenrirRenderer

| Option | For | Against |
|---|---|---|
| A. Render on the main thread, in a sequential system at the end of the frame (Bevy default, bgfx single-threaded, raylib) | Simplest; the GL context stays on the thread that created the window; no copy needed (§2 A works). | CPU render cost adds to frame time. |
| B. Render thread one frame behind, fed by an extracted snapshot or a command buffer (Bevy pipelined, Unreal, Godot threaded, bgfx, Filament) | Simulation and rendering overlap. | Needs §2 B or C; the GL context must live on the render thread; adds a frame of latency (Unreal: "a frame or two"); window events are usually on the main thread. |
| C. Jobs record commands in parallel (Unreal, Unity graphics jobs, bgfx encoders) | Scales draw recording on Vulkan and D3D12. | Not possible with one GL context. On GL, jobs can only build CPU-side draw lists or sort keys, which one thread then turns into GL calls (bgfx's encoders do this on every backend). |

All three can sit behind the same seam if the seam takes a draw list or command buffer rather than exposing immediate GL calls **(inference)**.

---

## 4. Sorting and batching

- **Bevy.** Each view has render phases (for example opaque, transparent, shadow).
  - *Binned phases* (such as `Opaque3d`) put items in bins by a `BinKey`. The key should be ordered "by the order of binding": pipeline, draw function, mesh, material bind group. Items in a bin are batched; batch sets can be multi-drawn (`crates/bevy_render/src/render_phase/mod.rs` L95-127, L2140-2160).
  - *Sorted phases* (such as `Transparent3d`) sort by a `SortKey`, usually view-space depth, back to front (L2189-2218).
  - Items are queued in `Queue`, sorted in `PhaseSort`, and drawn in `Render` (L1-25).
- **Filament.** Each command has a 64-bit `CommandKey`.
  - Depth and colour commands: channel, pass, priority, a 10-bit Z-bucket, then a 32-bit material id.
  - Blended commands: channel, pass, priority, inverted distance, blend order.
  - The high bits are needed "for correctness"; the low bits are optimisations that may be truncated (`filament/src/RenderPass.h` L68-126). Commands are sorted, then "instanceified" (L446-450).
- **bgfx.** A 64-bit key with the view in the top bits. Per-view modes: Default (program, then depth), Sequential (submission order, for UI), DepthAscending (front to back), DepthDescending (back to front). Keys are radix-sorted on the render thread (`docs/internals.rst` L4-27). It cites Christer Ericson's "Order your graphics draw calls around!" for the technique.
- **Unity SRP.** `DrawRenderers` takes a `SortingSettings` with `SortingCriteria`: `CommonOpaque` is roughly front to back with "optimization for reducing draw state changes", and `CommonTransparent` is back to front (Unity 6.2, `SortingCriteria`). Commands queue on `ScriptableRenderContext` until `Submit` (Unity 6.2, `ScriptableRenderContext`).
- **Unreal.**
  - `FMeshPassProcessor` turns each `FMeshBatch` into a stateless `FMeshDrawCommand`. Static meshes' commands are cached when the proxy is added.
  - The only merging is D3D11-style dynamic instancing of draws with identical bindings; per-primitive data lives in GPU Scene, indexed by `PrimitiveId`.
  - Setup, sorting and merging run as one task per pass ("Mesh Drawing Pipeline"). The docs do not describe the sort key.
- **raylib.** No sort. Vertices are batched until a state change, such as a texture or shader switch, forces a flush (`src/rlgl.h` L9-19).

**Common pattern:** opaque draws are sorted by state (pipeline or shader, then material, then mesh), with a coarse depth for early-Z. Transparent draws are sorted back to front by depth. UI and debug overlays keep submission order. A packed integer key makes the sort one radix or integer sort (Filament, bgfx) **(inference for the general pattern)**.

---

## 5. Debug drawing and text

### Debug drawing

- **im3d** (`3fd7afaf`).
  - An immediate-mode library for points, lines and triangles, plus 3D gizmos. It "outputs vertex buffers for rendering by the application", does not touch graphics state, and so can be called from anywhere (`README.md` L1-3).
  - Vertices are in world space and the view-projection is applied in the app's shader (L21).
  - Per frame: fill `AppData`, call `NewFrame()`, make calls, call `EndFrame()`, then render `GetDrawLists()`. Each `DrawList` is `{layerId, primType (triangles/lines/points), vertexData, vertexCount}` (`README.md` L45-49; `im3d.h` L45-58, L459-466).
  - It also emits `TextDrawList`s of `{position+size, colour, flags, string}` for the app to render (`im3d.h` L56-58, L478-488).
  - It has per-thread contexts that can be merged before `EndFrame` (`im3d.h` L214-219).
- **Bevy gizmos.** "An immediate mode drawing api … for visual debugging": `gizmos.line(a, b, colour)` from any system (`crates/bevy_gizmos/src/lib.rs` L7-20). Gizmo data is extracted and queued into the `Transparent2d`/`Transparent3d` phases (`crates/bevy_gizmos_render/src/lib.rs` L9-15, L143, L620-627).
- **Unreal.** `ULineBatchComponent` holds `BatchedLines`, `BatchedPoints` and `BatchedMeshes`, each with a lifetime (`DefaultLifeTime`, and a per-call `LifeTime`). `DrawLines` is "faster than calling DrawLine many times"; the class notes it is "not very performant for runtime use" (UE 5.8 API, `ULineBatchComponent`). The `UDrawDebugLibrary` docs say its `DrawDebugLines` should be preferred to `DrawDebugLine` because it batches (UE API, `UDrawDebugLibrary::DrawDebugLines`).
- **Godot.**
  - No built-in 3D debug-draw API was found in the 4.5 docs. The documented building block is `ImmediateMesh`, "similar to OpenGL 1.x immediate mode", which warns that complex geometry is "highly inefficient" and is "designed to generate simple geometry that changes often" (Godot 4.5 class reference).
  - Community add-ons fill the gap. This is a secondary source: a web search found no official proposal.
- **bgfx.** Transient vertex and index buffers are per-frame ring allocations "intended for dynamic geometry that changes every frame (e.g. debug rendering, particles, UI)" (`docs/internals.rst` L106-113). There is also a built-in debug text overlay, `dbgTextPrintf` (`include/bgfx/bgfx.h` L2780).

What Fenrir already has: FenrirPhysics produces `DebugLine { from, to, colour }` spans each step, drawn today in one `GL_LINES` pass (`.scratch/fenrir-renderer/notes.md`, "Debug lines").

### Text

- **stb_truetype** (v1.26).
  - It parses TrueType, rasterises antialiased one-channel bitmaps, and generates one-channel SDFs (`stbtt_GetGlyphSDF`) (`stb_truetype.h` L13-18, L948).
  - `stbtt_BakeFontBitmap` is a simple atlas baker, and `stbtt_PackBegin`/`PackFontRanges` is the better packer (L104-109).
  - Warning: "NO SECURITY GUARANTEE -- DO NOT USE THIS ON UNTRUSTED FONT FILES" (L6).
- **raylib** builds font atlases with stb_truetype, as bitmaps (`stbtt_GetCodepointBitmap`) or as SDF (`stbtt_GetCodepointSDF`) (`src/rtext.c` L700-712).
- **The Forge** uses Fontstash on stb_truetype with one atlas. It draws both screen-space text (`cmdDrawTextWithFont`, pixel coordinates) and world-space text (`cmdDrawWorldSpaceTextWithFont`, given a world matrix and the camera's view-projection) (`Common_3/Application/Interfaces/IFont.h` L105-115). It has separate 2D and 3D vertex shaders (`Common_3/Application/Fonts/Shaders/FSL/fontstash2D.vert.fsl`, `fontstash3D.vert.fsl`).
- **Bevy** lays text out with parley and rasterises glyphs on demand into `FontAtlas` textures, one atlas set per font size, with subpixel-offset bins (`crates/bevy_text/src/lib.rs` L13-30; `crates/bevy_text/src/font_atlas.rs` L20-31). UI text and world-space `Text2d` share the pipeline.
- **Godot** caches glyphs per font and per size. It can import fonts as MSDF so they render "at any size, without having to re-rasterize" (Godot 4.5 docs, "Using Fonts", MSDF section).
  - Costs it lists: a higher baseline cost, small sizes less clear than raster, no LCD subpixel, and self-intersecting outlines render wrongly.
  - Godot vendors msdfgen 1.13 (`thirdparty/README.md` L875-878).
- **msdf-atlas-gen** (v1.4) is an offline or library tool that packs glyphs into an atlas of hard mask, soft mask, SDF, PSDF, MSDF or MTSDF type. It writes the layout as JSON, CSV or Artery Font (`README.md` L3-30, L121-138).
  - MSDF keeps sharp corners when scaled up. MTSDF adds true distance in alpha for glows and outlines (README table).

**Trade-offs** (sources above):

| | Raster atlas (stb_truetype) | SDF (stb_truetype SDF) | MSDF / MTSDF (msdf-atlas-gen) |
|---|---|---|---|
| Size changes | Re-rasterise, or one atlas per size (Bevy) | One atlas scales | One atlas scales, with sharp corners |
| Small text | Best (Godot) | Softer | Less clear than raster (Godot) |
| Dependencies | One header | One header | msdfgen and msdf-atlas-gen; offline step or library |
| World-space text | Blurry when magnified **(inference)** | Good | Best |

**Screen-space against world-space text.** The engines draw both from the same glyph atlas and differ only in the vertex transform: pixel-space orthographic for screen text, and world matrix times view-projection for world text (The Forge `IFont.h` L105-112). World text can also be projected to a screen point and drawn as screen text, which keeps a constant pixel size; im3d's text lists give a world position plus a size for the app to place **(inference)**.

---

## 6. UI over the scene

### What a UI library needs, and what it hands back

- **Dear ImGui** (v1.93.0, `docs/BACKENDS.md`).
  - *The app gives it:* mouse and keyboard input in `ImGuiIO`; the ability to create, update and destroy textures; and the ability to draw "indexed textured triangles with a clipping rectangle" (L25-31).
  - *It gives back:* `ImDrawData` with a list of `ImDrawList`s, each with a vertex buffer `{float2 pos, float2 uv, u32 colour}`, an index buffer, and `ImDrawCmd`s (clip rect, texture id, index offset and count, vertex offset, optional user callback).
  - *State the backend sets:* alpha blending, no culling, no depth test or write, scissor on, bilinear sampling, and an orthographic projection over `DisplayPos..DisplayPos+DisplaySize` (L214-294).
  - Since 1.92 the backend also processes `ImDrawData->Textures` (create, update and destroy requests). That pointer is kept in `ImDrawData` "to allow advanced users to perform multi-threaded rendering" (L295-324).
- **RmlUi** (6.3, `Include/RmlUi/Core/RenderInterface.h`).
  - *The app implements:* `CompileGeometry(vertices, indices) -> handle`, `RenderGeometry(handle, translation, texture)`, `ReleaseGeometry`; `LoadTexture`, `GenerateTexture`, `ReleaseTexture`; and `EnableScissorRegion`/`SetScissorRegion` (L34-70).
  - *Optional:* clip masks, `SetTransform` (a 4x4 matrix), and a layer stack (`PushLayer`, `CompositeLayers` with blend modes and filters, `PopLayer`, `SaveLayerAsTexture`) for effects (L76-112).
  - RmlUi uses retained geometry handles where ImGui rebuilds buffers every frame.
- **The Forge.** `cmdDrawUserInterface(cmd)` records ImGui into the command buffer it is given, after the app binds the swapchain with `LOAD_ACTION_LOAD` and no depth (`IUI.h` L507-513; `01_Transformations.cpp` L1019-1031).
- **Bevy UI** is its own library.
  - UI nodes are extracted into the render world (`extract_uinode_*` in `ExtractSchedule`).
  - `ui_pass` runs after `Core3dSystems::PostProcess` and before upscaling, so UI is drawn over the tonemapped scene at output resolution (`crates/bevy_ui_render/src/lib.rs` L267-302).
- **bgfx.** UI goes in its own view with `Sequential` mode, which keeps painter's order (`docs/internals.rst` L11-14).

### Options for what FenrirRenderer offers a UI library

The map says ImGui will be replaced by the user's own GUI library, so the question is the shape of the seam:

| Option | What the renderer offers | For | Against |
|---|---|---|---|
| A. A "UI geometry" API in ImGui/RmlUi shape | Upload or create textures; draw a list of `{vertices (pos2, uv, colour), indices, clip rect, texture}` in an overlay pass after the scene (and after any post-processing), in submission order, with alpha blending, no depth, and a screen-space orthographic projection. | Fits both ImGui's `ImDrawData` and RmlUi's `RenderInterface` with thin adapters; the library never sees GL; a future GUI library can target the same API. | Effects such as RmlUi layers or filters need more (render targets, blend modes). |
| B. A callback in a UI pass that hands the library a backend handle | The library draws with the graphics API directly (The Forge hands a `Cmd*`). | Least renderer code. | Breaks ADR-0010's rule against exposing the library underneath; ties UI code to GL. |
| C. A built-in UI (Bevy) | — | — | Out of scope on the map. |

Option A also covers debug text if text quads go through the same overlay pass **(inference)**.

---

## Summary of the options

- **Passes**: fixed passes (A), an ordered pass list (B), passes as scheduler systems (C), a declared graph (D), or barriers inferred in a later backend (E). A graph pays off with explicit APIs, many intermediate targets, and many teams or features. On GL with a few targets its main gain is code structure, not memory or barriers.
- **ECS to renderer**: draw directly (A), a per-frame extracted snapshot (B), or persistent proxies (C). Interpolation goes either in a post-tick system using `accumulator / tickRate`, or in the extraction copy.
- **Threads**: GL allows one context per thread, so rendering is single-threaded on GL unless jobs only build CPU-side draw lists. A render thread one frame behind needs §2 B or C. Parallel recording needs Vulkan or D3D12.
- **Sorting**: a packed 64-bit key (pass, then state for opaque, depth for transparent, sequence for UI and debug), sorted once per frame.
- **Debug and text**: an immediate-mode line/point/triangle buffer per frame (im3d, Bevy gizmos, bgfx transient buffers). Text uses an atlas: raster (stb_truetype) is simplest, SDF scales with one header, and MSDF is sharpest when scaled but adds a tool.
- **UI**: a textured-triangles-with-scissor overlay API is the common contract of ImGui and RmlUi.
