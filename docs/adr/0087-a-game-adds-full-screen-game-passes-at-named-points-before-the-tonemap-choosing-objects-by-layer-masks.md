---
status: accepted
---

# A game adds full-screen game passes at named points before the tonemap, choosing objects by layer masks

ADR-0019 kept passes the renderer core's own: "game code and other packages do not add passes". A game that wants a look the renderer does not ship, such as Flightless Frenzy's dark outlines on its characters and pickups but not its level, then has no way to get it except faking it in its meshes. So a game may now add **game passes**: full-screen fragment passes the renderer runs at two named points in each camera view's frame, reading the frame's own textures and **layer masks** of the entities it chooses. The renderer still decides when it runs them and what they read; the game writes what they do. It was settled in a grilling session on 2026-11-19 (`.scratch/engine-gaps/issues/23-how-a-game-draws-outlines-and-adds-a-pass.md`, spec `.scratch/fenrir-game-passes/spec.md`).

## The decision

- **A general block, not an outline feature.** The engine chooses no outline style; a game writes its outlines, a hit flash or a menu blur as a game pass.
- **Two points, both before the tonemap:** after opaque (with the background drawn) and before transparents, and after transparents and before debug drawing, bloom and the tonemap. What a pass writes is tonemapped with the rest. No point follows the tonemap, because it sits inside the one `Output` draw (ADR-0059).
- **Inputs on request:** a snapshot of scene colour, scene depth and scene normals (which the depth prepass already makes), resolved from MSAA when the view is multisampled, and layer masks. Nothing is produced, resolved or drawn for an input no pass asks for.
- **Layer masks choose objects:** `MeshRenderer` and `InstancedMeshRenderer` carry a `layers` bitmask, and a pass asks for the mask of some layers, which the renderer draws into a target of the pass's own. There is no stencil to build on, and a bitmask serves passes, decals and later features alike.
- **Declared from C++ or C#,** naming a shader asset, a point and its inputs, with parameters set by name as a material's are. The shader is a shader container loaded into the asset store like a material shader.
- **Fragment passes only, for now.** Compute passes in the `Cull` stage (ADR-0057's open slot) come later.

## Considered options

- **An outline feature in the renderer** (Godot's stencil outline presets). Rejected: it decides a look for the game, and serves nothing else.
- **A front-face cull mode, for inverted hulls.** Rejected as the block: an outline would then be a second draw of every outlined mesh, and the game's rule is that outlines do not come from its meshes.
- **A point after the tonemap.** Not offered: the tonemap and the output write are one draw.

## Consequences

- ADR-0019's "game code and other packages do not add passes" is amended: a game adds game passes, at the points this ADR names; packages still add none.
- The frame gains a resolve of depth and normals under MSAA, and a mask draw per asked-for layer set, both only when a pass asks.
