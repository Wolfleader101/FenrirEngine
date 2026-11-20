# Game passes

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [How do engines let a game add a render pass and draw outlines?](../engine-gaps/issues/22-how-engines-add-render-passes-and-outlines.md) and [How does a game draw outlines and add its own render pass?](../engine-gaps/issues/23-how-a-game-draws-outlines-and-adds-a-pass.md). Settles [A game cannot draw outlines or add a render pass](../engine-gaps/issues/07-outlines-and-game-render-passes.md). ADR-0087.

## Problem Statement

A game's material shader can shade its surfaces however it likes, but it cannot draw anything across the frame: it cannot read scene depth or normals, there is no stencil, and the render graph admits no pass from game code. So screen-space outlines, the other half of a cel-shaded look, are out of reach, and so is any full-screen effect a game might want (a hit flash, a blur behind a menu). An inverted-hull outline is not possible either, since a material cannot cull front faces, and Flightless Frenzy's rule is that outlines do not come from tricks in its meshes. Flightless Frenzy wants dark outlines on Kiki, the friends, the enemies and the pickups, and none on the level.

## Solution

A game adds **game passes**: full-screen fragment passes, declared from C++ or C#, that the renderer runs in every camera view at one of two named points before the tonemap (after opaque, or after transparents). A pass names a shader asset and the inputs it reads (a snapshot of scene colour, scene depth, scene normals, and layer masks), and writes into scene colour. Every `MeshRenderer` and `InstancedMeshRenderer` gains a `layers` bitmask, and a pass asks for a mask of some layers, which the renderer draws for it, so a pass chooses which objects it affects. Inputs no pass asks for cost nothing. RendererDemo gains an Outlines scene.

## User Stories

1. As a game developer, I want to add a full-screen pass to the frame, so that I can draw a look the renderer does not ship.
2. As a game developer, I want to run my pass after opaque surfaces and the sky, before transparents, so that outlines sit under glass and particles.
3. As a game developer, I want to run my pass after transparents, before bloom and the tonemap, so that a full-screen effect covers everything in the world.
4. As a game developer, I want what my pass writes tonemapped with the rest of the frame, so that my outlines match the scene's colours.
5. As a game developer, I want my pass to read scene depth, so that I can find silhouettes.
6. As a game developer, I want my pass to read scene normals, so that I can find creases.
7. As a game developer, I want my pass to read a snapshot of scene colour, so that I can blur or tint what is already drawn.
8. As a game developer, I want depth and normals given to me resolved when MSAA is on, so that my pass works the same with or without it.
9. As a game developer, I want to put entities on render layers with a bitmask, so that I can say which objects an effect applies to.
10. As a game developer, I want my pass to ask for a mask of some layers, so that I outline Kiki, the friends, enemies and pickups and not the level.
11. As a game developer, I want a layer mask to carry the masked entities' own depth, so that my outline can tell when the object is behind something else.
12. As a game developer, I want instanced renderers to carry layers too, so that every coin in an instance list is outlined.
13. As a game developer, I want new renderers on a default layer, so that nothing changes until I set layers.
14. As a game developer, I want to write my pass as a shader container loaded like my material shaders, so that it builds with the same tools.
15. As a game developer, I want a shared shader include that gives my pass its inputs, parameters and the view's matrices, so that I write only the effect.
16. As a game developer, I want to set my pass's parameters by name, as I set a material's, so that I can change an outline's colour and width at run time.
17. As a game developer, I want to choose how my pass's output blends into scene colour, so that an outline can be drawn over the scene without copying it.
18. As a game developer, I want to enable, disable and remove a pass, so that an effect can come and go.
19. As a game developer, I want passes at the same point to run in the order I added them, so that I control how effects stack.
20. As a game developer, I want my pass to run in every camera view, so that split screens and picture-in-picture views get it too.
21. As a C# game developer, I want to add a pass, set its parameters and set renderers' layers from C#, so that my C++ host only loads shaders.
22. As a game developer, I want a pass with a missing shader or an input the frame cannot give to log an error and be skipped, so that one bad pass does not break the frame.
23. As an engine maintainer, I want no input produced, resolved or masked that no pass asks for, so that a game with no passes pays nothing.
24. As an engine maintainer, I want game passes recorded in an ADR amending ADR-0019, so that a later reader knows why game code may now add passes and where.
25. As an engine maintainer, I want an Outlines scene in RendererDemo, so that I can see a game pass working by hand.

## Implementation Decisions

- **Two game pass points** in each camera view's frame: **after opaque**, once the background is drawn and before transparents; and **after transparents**, before debug drawing, post-processing and `Output`. They are new places in the render stage order (ADR-0019), not shadow or overlay views. Passes at one point run in the order added.
- **A game pass** is a full-screen fragment pass described by: a shader asset (a shader container in the asset store, loaded by whoever owns it; Flightless Frenzy's host loads it beside its material shaders), a point, the inputs it reads, a blend mode into scene colour (replace, alpha or additive), parameters, and whether it is enabled. The renderer core adds it to each camera view's render graph through a game-pass render feature of its own; game code never touches the graph.
- **Inputs, each only on request:**
  - **scene colour**: a snapshot of scene colour at the pass's point, copied (and resolved under MSAA) only when asked; the pass's output goes into scene colour itself;
  - **scene depth** and **scene normals**, from the depth prepass, resolved to single-sample targets once per point under MSAA when any pass there asks;
  - **layer masks**: for each distinct set of layers asked for at a point, the renderer draws the opaque and masked entities on any of those layers into a target holding their coverage and their own depth, at the view's size.
- **Layers.** `MeshRenderer` and `InstancedMeshRenderer` gain `layers`, a 32-bit bitmask, default layer 0's bit. Layers are numbers; naming them is the game's. A later feature (decals) reads the same bitmask.
- **The pass shader** includes a shared shader include that declares the inputs a pass may bind, the view's matrices and size, the pass's parameter block and a full-screen triangle vertex stage. Parameters are reflected from the shader and set by name, as material parameter overrides are, with the shader's defaults otherwise.
- **The API:** from C++, the renderer adds, finds, enables, disables and removes game passes and sets their parameters, returning a handle; from C#, the same through `Renderer.AddPass(name, shader, point, inputs)` and the handle's calls, plus `layers` on the renderer components, through the generated bindings.
- **Errors:** a pass whose shader is missing or not a game pass shader, or that asks for an input the frame cannot give, logs one error and is skipped until changed.
- **Fragment passes only.** Compute game passes and a point after the tonemap are not in this spec.
- **Glossary:** Game pass and Layer mask are already defined.

## Testing Decisions

- **A good test here builds a frame and checks the graph and the pixels**: which passes run where and in what order, which inputs exist, what a mask holds, what the frame looks like; never how the feature is written.
- **Null-backend renderer tests** (prior art: the render graph and depth prepass tests):
  - a game pass runs at its point, after the background or after transparents as asked, and before debug and post-processing;
  - passes at one point run in the order added; disabled and removed passes do not run;
  - with no game passes, or with passes asking for nothing, no snapshot, resolve or mask target is made;
  - a layer mask draws exactly the opaque and masked entities on its layers, instanced ones included, and no transparent one;
  - under MSAA, depth and normals are resolved for a pass that reads them;
  - a pass with a missing shader or an impossible input logs an error and is skipped.
- **A GL golden-image test** (prior art: the golden images, label `gl`, skipped without a GPU): a scene with one entity on an "outlined" layer and one not, drawn with an outline game pass reading the layer mask and depth, compared with a committed golden.
- **FenrirRendererScripting's fixture tests:** a pass added from C# runs; its parameters set from C# reach the pass; `layers` written from C# reaches the renderer component.
- **By hand, RendererDemo's new Outlines scene:** an outline game pass draws dark outlines around objects on an "outlined" layer and not around the floor, with keys to change the colour and width and to move an object on and off the layer, listed in the scene's help and on its demo panel.

## Out of Scope

- Compute game passes, and a user pass in the `Cull` stage.
- A point after the tonemap, and passes in shadow views or the overlay.
- A built-in outline effect or any other built-in game pass.
- Stencil buffers and front-face culling on materials.
- Per-camera choice of which passes run.
- Decals reading layers: their own spec.

## Further Notes

- The facts behind the decisions, with file and line, are in [How do engines let a game add a render pass and draw outlines?](../engine-gaps/issues/22-how-engines-add-render-passes-and-outlines.md) (its findings on the local `research/game-render-passes` branch) and [How does a game draw outlines and add its own render pass?](../engine-gaps/issues/23-how-a-game-draws-outlines-and-adds-a-pass.md).
- The depth prepass uses the built-in fragment stage, so a game material shader's `discard` does not reach scene depth or the layer masks; masked materials use the prepass's own alpha test as today.
- Build and test only `msvc-x64-debug` locally, run the GL tests on your own GPU before a PR that touches the OpenGL backend, and use the short-path junction in a worktree (`README.md`, "Building and testing").
