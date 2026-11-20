# How do engines let a game add a render pass and draw outlines?

Type: research
Status: resolved
Blocked by: none
Settles: [A game cannot draw outlines or add a render pass](07-outlines-and-game-render-passes.md)

## Question

How do Unity (URP renderer features, HDRP custom passes), Godot 4 (compositor effects, `next_pass`, depth and normal textures in shaders), Unreal (post-process materials, custom depth and stencil) and a lighter engine let a game add a pass or a post-process: where in the frame, reading what (colour, depth, normals, stencil, per-object masks), declared how? Which outline techniques exist (inverted hull, screen-space edge detection on depth and normals, stencil or mask outlines, jump flood), what does each need from the engine, and how is an outline limited to some objects? What does FenrirRenderer's frame offer today?

Findings go in `.scratch/engine-gaps/research/game-render-passes.md` on the `research/game-render-passes` branch.

## Answer

Researched 2026-11-19: `.scratch/engine-gaps/research/game-render-passes.md` on the local branch `research/game-render-passes` (6ebc93e4), never merged (`git show research/game-render-passes:.scratch/engine-gaps/research/game-render-passes.md`).

- **Every engine runs a game callback at a named point in the frame** and lets it read the frame's own textures, some only on request: URP's `ScriptableRendererFeature` at 18 `RenderPassEvent` points, depth and normals through `ConfigureInput`; HDRP's custom pass volumes at 7 injection points, each with a fixed read and write table, plus custom colour and depth buffers; Godot's `CompositorEffect` at 5 callback points, or a full-screen quad reading `hint_depth_texture` and `hint_normal_roughness_texture`; Unreal's post-process materials by blendable location, or a C++ `SceneViewExtension`; Bevy 0.19+ through systems in the `Core3d` schedule.
- **Outlining only some objects:** Unity by layer mask (URP Render Objects, HDRP `DrawRenderers` into a custom buffer); Godot by a material's `next_pass` with grow and `cull_front`, or 4.5's stencil outline presets; Unreal by a per-object custom-depth flag and stencil value read in a post-process material; Bevy by the `bevy_mod_outline` crate.
- **What each technique needs:** inverted hull, a second draw culling front faces; screen-space edges, depth and normals a pass can sample; mask, dilate or jump flood, drawing a chosen set of objects into an extra target and full-screen passes; stencil outlines, per-material stencil state.
- **FenrirRenderer today:** an internal render graph with no hook for a game (ADR-0019: game code and other packages add no passes; ADR-0057 calls `Cull` open to user passes with no API behind it). Every view's prepass already makes scene depth and normals, but no material binds them (`View.glsl` set 1). The tonemap happens inside the one `Output` draw (ADR-0059), so nothing sits between the tonemap and the target. `CullMode` is only none or back (`Resources.hpp:263-268`), there is no stencil, a material has one shader and no second pass (ADR-0024), renderers carry no layer, mask or object id, and the prepass uses the built-in fragment stage, so a game shader's `discard` never reaches it.
- **Sources:** several Epic pages (scene view extensions, custom depth) came back empty and rest on search extracts, marked so; Ben Golus's wide-outlines article was not readable.

What [How does a game draw outlines and add its own render pass?](23-how-a-game-draws-outlines-and-adds-a-pass.md) chooses between is listed at the end of the findings.
