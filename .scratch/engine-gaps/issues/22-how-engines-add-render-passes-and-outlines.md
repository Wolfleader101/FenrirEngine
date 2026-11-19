# How do engines let a game add a render pass and draw outlines?

Type: research
Status: open
Blocked by: none
Settles: [A game cannot draw outlines or add a render pass](07-outlines-and-game-render-passes.md)

## Question

How do Unity (URP renderer features, HDRP custom passes), Godot 4 (compositor effects, `next_pass`, depth and normal textures in shaders), Unreal (post-process materials, custom depth and stencil) and a lighter engine let a game add a pass or a post-process: where in the frame, reading what (colour, depth, normals, stencil, per-object masks), declared how? Which outline techniques exist (inverted hull, screen-space edge detection on depth and normals, stencil or mask outlines, jump flood), what does each need from the engine, and how is an outline limited to some objects? What does FenrirRenderer's frame offer today?

Findings go in `.scratch/engine-gaps/research/game-render-passes.md` on the `research/game-render-passes` branch.
