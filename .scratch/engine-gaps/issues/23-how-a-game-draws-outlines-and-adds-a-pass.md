# How does a game draw outlines and add its own render pass?

Type: grilling
Status: resolved
Blocked by: 22
Settles: [A game cannot draw outlines or add a render pass](07-outlines-and-game-render-passes.md)

## Question

Flightless Frenzy wants dark outlines on Kiki, friends, enemies and pickups and none on the level ([What does the game look like?](../../flightless-frenzy/issues/12-the-look.md)), from the engine, with no trick in the game's meshes. With [How do engines let a game add a render pass and draw outlines?](22-how-engines-add-render-passes-and-outlines.md) in hand:

- **Which building block:** a game-defined pass or post-process (the general block, outlines written by the game in it), an outline feature in the engine, a material cull-mode setting that makes an inverted hull possible, or more than one.
- **What a game pass can read:** scene depth, a normals target, stencil or a per-object mask; which of these the forward frame must start producing, and at what cost when no pass reads them.
- **Where it sits** in the frame relative to opaque, transparents, bloom, the tonemap and the UI.
- **How it is declared:** from C++ in the host (where the game registers its shaders today, [C++, C#, or a split?](../../flightless-frenzy/issues/04-cpp-csharp-or-a-split.md)), from C#, or as data.
- **Choosing objects:** how a game marks which entities get an outline.
- Whether this is an ADR (the frame's shape).

Outcome: a spec-ready design.

## Facts (checked 2026-11-19)

- No stencil (the window has none, renderer README:33). Material shaders bind no scene depth or normals (`Shaders/include/Fenrir/View.glsl:46-69`, `Frame.glsl`).
- `RenderStage` (`packages/FenrirRenderer/src/Core/RenderGraph.hpp:24-44`) calls `Cull` "open to a user compute pass", but the graph is internal; README:40 says there is no native escape hatch.
- Frame order (README:54): post-processing after Debug in `PostProcess` (scene history, bloom, streaks, lens flare), then `Output` (exposure, vignette, grading, tonemap, grain, sRGB), then Overlay.

## Answer

Grilled 2026-11-19 in round 3 of [Engine gaps for Flightless Frenzy](../map.md); every recommendation was accepted. It rests on [How do engines let a game add a render pass and draw outlines?](22-how-engines-add-render-passes-and-outlines.md). **Spec-ready, with an ADR** amending ADR-0019's "game code and other packages do not add passes".

- **The block is a game pass,** not an outline feature and not a front-face cull mode: a full-screen pass a game adds to the frame, in which it writes its own outlines or any later effect (a hit flash, a menu blur). The engine chooses no outline style.
- **Two points in the frame,** both before the tonemap, so what a pass writes is tonemapped with the rest:
  - after opaque, before transparents;
  - after transparents, before bloom and the tonemap.

  The tonemap sits inside the one `Output` draw (ADR-0059), so no point after it is offered. The first version runs full-screen fragment passes only; compute passes (the `Cull` stage's open slot, ADR-0057) come later.
- **What a pass reads:** scene colour, scene depth and scene normals, which every view's prepass already makes (`View.glsl` set 1 binds none of them to materials today), and layer masks. Nothing is produced for a pass that no pass asks for.
- **Choosing objects:** `MeshRenderer` and instanced renderers gain a `layers` bitmask. A pass asks for the mask of a layer, and the engine draws the entities on it into a target of the pass's own. Flightless Frenzy puts Kiki, friends, enemies and pickups on an "outlined" layer and leaves the level off it. (There is no stencil to build on.)
- **Declaring one:** from C#, `Renderer.AddPass(name, shader, point, inputs)`, with the pass's parameters (an outline's colour and width) set from C#. The pass's shader is registered in the C++ host beside the material shaders, since CMake builds shaders, so the host does not grow.
- **The ADR** records game passes at named points before the tonemap, the inputs they may read, and layer masks as how a pass chooses objects; its number is taken when the spec is written.
