# How is a frame built and submitted?

Type: grilling
Status: resolved
Blocked by: 05, 08

## Question

How does FenrirRenderer turn the active scene into a frame?

- Fixed passes or a render graph in the foundation; what passes the foundation has (opaque, skybox or background, debug lines and text, the UI library's overlay) and how a later family (shadows, post-processing) adds one.
- Main thread or a render thread; does the renderer read the ECS directly or extract into its own copy?
- Which FenrirApp phases the renderer's systems run in, relative to `AfterTicks` (interpolated transforms), C# `Update` systems and window event pumping; and where present happens.
- Sorting and batching, and what is left out of the foundation.

## Answer

ADR: `docs/adr/0019-fenrirrenderer-builds-each-frame-as-a-render-graph-on-the-main-thread.md`. Glossary: Renderer core, Draw list, Render graph, Render pass, Render stage, Render feature, View.

- **Three layers:** ECS facade → renderer core (own CMake target, no FenrirApp, ECS or Scene dependency, headers not installed, tested with the null backend and no `App`) → private graphics device.
- **When:** a new once-per-frame `Render` phase after `PostUpdate`, before the event queues swap, so it sees `AfterTicks`' interpolated poses and everything C# wrote. Window events are pumped first in `PreUpdate`; present is the last step of `Render`.
- **Thread:** the main thread. A render thread is wanted eventually, as most engines have; the plain-data draw list is the snapshot it would consume.
- **Out of the ECS:** direct reads of the active scene into a per-frame draw list (mesh, material, world matrix, 64-bit sort key); one draw per item, no instancing or culling.
- **Render graph,** compiled every frame: passes declare precise resource usage, a queue kind and a record function into their own `CommandList`; order by a fixed render-stage enum (`Shadow, Opaque, Background, Transparent, Debug, PostProcess, Output, Overlay`), then registration order; dependencies cull and validate; transients pooled with computed lifetimes; barriers derived in the backend from declared usage; `framesInFlight` (default 2) ring-buffers per-frame data. Render features inside the core add passes; game code does not.
- **Designed as if explicit:** memory aliasing, async compute and copy queues, parallel recording, split barriers, pipeline caches and a render thread are shaped for, not built, and listed in the ADR.
- **Foundation passes:** `Opaque → Background → Transparent → Debug → Output → Overlay`. Transparent items are depth tested without depth write, sorted back to front by origin, blend mode from the material; no OIT.
- **Targets:** per view, linear RGBA16F `SceneColor` and `SceneDepth` with `RendererConfig`'s MSAA; `Output` resolves, clamps and sRGB-encodes (the post-processing slot); the overlay draws once on the backbuffer. One graph holds every view; render-to-texture views order by dependency.
