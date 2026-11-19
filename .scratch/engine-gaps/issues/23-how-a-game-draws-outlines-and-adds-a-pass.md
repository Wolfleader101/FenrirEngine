# How does a game draw outlines and add its own render pass?

Type: grilling
Status: open
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
