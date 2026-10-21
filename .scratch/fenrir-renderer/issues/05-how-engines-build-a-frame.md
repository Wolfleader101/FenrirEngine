# How do engines build and submit a frame?

Type: research
Status: resolved
Blocked by: none

## Question

How do engines turn their scene into GPU work each frame, in the areas FenrirRenderer's frame design hangs on? Read Bevy (render world and extraction), Godot 4, Unreal, Unity's SRP and render graph, Filament, The Forge and the Frostbite frame-graph talk, plus at least one small engine with fixed passes:

- **Passes:** a render graph (declared passes and resources, automatic barriers and aliasing) against fixed passes in code; what a small engine gains from a graph, and when one pays off.
- **Getting data out of the ECS:** extraction into a render world (Bevy), render proxies (Unreal), or drawing straight from components; and how interpolated transforms reach the renderer.
- **Threads:** rendering on the main thread, on a render thread one frame behind, or recording commands from jobs; what GL allows (one context per thread) against Vulkan and D3D12.
- **Sorting and batching:** draw lists, sort keys, state changes.
- **Debug drawing and text:** immediate-mode debug draw (im3d, Godot's and Unreal's debug draw), and text rendering (stb_truetype atlases, signed distance fields, msdf-atlas-gen); screen-space and world-space text.
- **UI over the scene:** how an engine lets a UI library (Dear ImGui, RmlUi, its own) draw over the frame: what it hands the library and what the library hands back.

Write findings to a Markdown file on the `research/frame-building` branch (`.scratch/fenrir-renderer/research/frame-building.md`), citing each claim's source and pinning the versions read, and link it here.

## Answer

Findings: `.scratch/fenrir-renderer/research/frame-building.md` on branch `research/frame-building` (commit `0c46bee`), against Bevy v0.20.0, Godot 4.7.2, Filament v1.78.0, The Forge v1.63, bgfx `21a5cf8f`, raylib 6.2, im3d `3fd7afaf`, Dear ImGui v1.93.0, RmlUi 6.3, msdf-atlas-gen v1.4 and stb_truetype v1.26, plus the UE 5.8 and Unity 6.2 docs, the GL 4.6 spec, Vulkan and D3D12 docs and the Frostbite GDC 2017 slides, each claim cited. Gaps: Unreal from docs only; Unity's interpolation page unread; Godot has no official debug-draw API; inferences are marked.

- **Graphs pay off with explicit APIs and big frames:** Frostbite, Unreal RDG, Unity URP and Filament declare passes and resources, cull unused passes, alias transient memory and insert barriers; Frostbite's aliasing about halved intermediate target memory and its render code fell from 15k to 5k lines.
- **Small engines use fixed passes** (Godot, The Forge, bgfx's ordered views, raylib). On GL the driver handles barriers, so a graph buys structure only; Godot builds a graph from recorded commands below its fixed passes for automatic barriers, a layer a future Vulkan or D3D12 backend could hold.
- **Bevy 0.20 replaced its node graph with ECS schedules:** each camera runs a `Core3d` chain of system sets, features slotted in with `.after()`/`.before()`. FenrirScheduler has no such ordering and no render phase.
- **Getting data out of the ECS:** draw from components directly, snapshot into a render world each frame (Bevy's sync and extract), or keep persistent render proxies (Unreal, Godot's RenderingServer); a render thread a frame behind needs one of the latter two.
- **Interpolation** happens after the fixed-step loop (Bevy's slot, Godot's `SceneTreeFTI`), showing state 1–2 ticks behind; Fenrir already has the fraction as `accumulator / tickRate` after `Tick`.
- **Threading:** GL allows one current context per thread and shares no VAOs or FBOs, so GL calls come from one thread; Vulkan and D3D12 record in parallel with a pool per thread; Bevy, Unreal, Godot, bgfx and Filament can render a frame behind, and bgfx's encoders build draw lists from jobs on every backend. In Fenrir, rendering systems would be sequential on the context's thread.
- **Sorting:** packed 64-bit keys (Filament, bgfx): pass in the high bits, then state for opaque, back-to-front depth for transparent, submission order for UI; Bevy bins opaque draws by pipeline, material and mesh.
- **Debug drawing** is an immediate buffer rebuilt each frame (im3d, Bevy gizmos, Unreal's line batcher, bgfx transient buffers); im3d hands back world-space line, point, triangle and text lists.
- **Text** comes from a glyph atlas: stb_truetype raster (simplest, one atlas per size), stb SDF (scales, one header), MSDF (sharpest scaled, softer small, adds a tool). Screen and world text share the atlas and differ only in the vertex transform.
- **UI libraries share one contract:** Dear ImGui and RmlUi need texture create, update and release plus indexed, textured, coloured triangles with a scissor rectangle, drawn in an overlay pass after post-processing with blending and no depth; a renderer-owned UI geometry API can hide GL.
