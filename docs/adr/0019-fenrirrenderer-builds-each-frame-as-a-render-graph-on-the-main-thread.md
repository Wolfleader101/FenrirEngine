---
status: accepted
---

# FenrirRenderer builds each frame as a render graph on the main thread

Each frame, FenrirRenderer reads the active scene into a draw list, builds a render graph of passes from it, records each pass into a command list and submits them through the graphics device (ADR-0016), all on the main thread, in a new `Render` phase of FenrirApp. Only OpenGL is built, but the graph is designed as if the backend were Vulkan or D3D12: what GL cannot use is shaped for now and listed below.

## Layers

- **Three layers.** The **ECS facade** (components, the `Render`-phase systems, `Install`) → the **renderer core** (meshes, materials, the draw list, the render graph, render features, recording) → the private graphics device. The core is its own CMake target with no FenrirApp, FenrirECS or FenrirScene dependency, so the build enforces the line; its headers are not installed, as FenrirScripting's facade sits over Iris but with no second public API to freeze. The core is tested with the null backend and no `App`.

## The frame

- **A `Render` phase.** FenrirApp gains one once-per-frame phase, `Render`, after `PostUpdate` and before the event queues swap. It sees the poses interpolated in `AfterTicks` and everything C# wrote in `Update` and `PostUpdate`, without depending on `Install` order. Writing scene state in `Render` is unsupported. Window events are pumped first in `PreUpdate`; present is the last step of `Render` (who presents is the graphics-context decision's).
- **The main thread.** Building, recording and `Submit` run on the main thread, where the GL context is current.
- **A draw list from direct reads.** The facade reads the active scene's components directly into a per-frame, plain-data **draw list**: per item a mesh, a material, a world matrix and a packed 64-bit sort key (stage in the high bits; then pipeline, material and mesh for opaque items, back-to-front depth for transparent ones, submission order for overlay items). No render world kept in sync and no persistent proxies. Each item is one draw: no automatic instancing, merging or culling in the foundation.

## The render graph

- **Compiled every frame.** Each pass declares the graph resources it uses, with precise usage (colour attachment, depth attachment read or write, sampled, storage, copy source or destination), a queue kind (`Graphics`, `Compute`, `Copy`) and a record function that records through the per-pass encoder into its own `CommandList`. Passes whose outputs nothing reads are culled; a pass reading what nothing wrote is an error. The graph computes each transient's first and last use, allocates transients from a pool keyed by description, and imports external resources (the backbuffer, a camera's texture).
- **Order from stages, then dependencies.** Each pass names a **render stage** from a fixed enum: `Shadow, Opaque, Background, Transparent, Debug, PostProcess, Output, Overlay`. Passes run in stage order and, within a stage, in registration order; dependencies drive culling and validation. This is Unity's `RenderPassEvent` idea, chosen because several passes write the same target.
- **Render features add passes.** Each feature (the foundation materials, background, debug, overlay; later shadows, post-processing) lives in the renderer core and adds its passes to the graph builder each frame. Game code and other packages do not add passes.
- **Barriers stay below the seam.** A pass's declared usage goes down in its pass descriptor and the backend derives barriers from it, so ADR-0016's rule that the renderer never sees barriers holds. The GL backend ignores them.
- **Frames in flight.** `framesInFlight` (default 2) ring-buffers per-frame buffers (uniforms, debug vertices) and the transient pool by frame index, and feeds ADR-0016's deferred destruction.
- **One graph holds every view.** Each camera's view is a chain of passes in the same graph; a view drawing into a texture another pass samples is ordered before it by dependency. The overlay comes last, once per window.

## The foundation's passes

- `Opaque` (depth write) → `Background` (skybox or clear colour at the far plane, after opaque so covered pixels are skipped) → `Transparent` → `Debug` → `Output` → `Overlay`.
- **Transparent:** depth tested, no depth write, sorted back to front by the view-space depth of each item's world origin. A material's blend mode (opaque, alpha-blended, additive) picks the pass; both foundation materials can blend. No order-independent transparency; intersecting or overlapping transparent meshes can sort wrongly, and that is documented rather than fixed.
- **Targets:** each view draws into transient linear RGBA16F `SceneColor` and `SceneDepth`, multisampled by `RendererConfig`'s MSAA count. `Output` resolves, clamps and sRGB-encodes into the view's output: the placeholder tonemap a post-processing family replaces. `Overlay` draws on the backbuffer, in sRGB, after every view's `Output`.

## Waiting for an explicit backend

Designed for, not built, because GL cannot use them:

- **Memory aliasing of transients:** lifetimes are computed; nothing aliases.
- **Async compute and copy queues:** passes carry a queue kind; all run in order on one queue.
- **Parallel recording:** each pass has its own command list; one thread calls the record functions.
- **Split barriers:** usage is declared per pass; the GL backend derives no barriers.
- **Pipeline caches:** pipelines are created through the device; nothing persists them.
- **A render thread:** most engines render a frame behind on their own thread, and FenrirRenderer should eventually. The draw list is plain data built in one place, so it can become the snapshot a render thread consumes; the public API need not change.

Also deferred, not tied to a backend: a depth prepass for transparent items.

## Considered options

- **Fixed passes in code, with named insertion points** (Godot, The Forge, bgfx, raylib). Rejected: on GL it would do, but the graph is the layer Vulkan and D3D12 backends want, and it is harder to retrofit under families written against fixed passes than to start with.
- **Ordering by system sets in the scheduler** (Bevy 0.20). Rejected: FenrirScheduler has no before/after ordering, and rendering would lean on the app's schedule.
- **Rendering in `PostUpdate`.** Rejected: C# `PostUpdate` systems share the phase, so what the renderer sees would depend on `Install` order.
- **A render world or render proxies.** Rejected for now: they exist to feed a render thread; the draw list is the seam one would use.
- **The core as one target with a directory convention**, or **a standalone installable library** like Iris. Rejected: the first is not enforced; the second adds a public API nothing needs yet.

## When to revisit

When an explicit backend is built (take up the waiting list), when the frame is CPU-bound in recording (parallel recording or a render thread), or when transient-target memory shows up.
