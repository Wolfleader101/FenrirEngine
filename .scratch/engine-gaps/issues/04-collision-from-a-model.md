# A level model does not give the level its collision

Status: needs-triage
Blocked by: none

## What is missing

Collision meshes and hulls are cooked from vertices the caller supplies (`CreateCollisionMesh`). Nothing takes a loaded model, or the entities `SpawnModel` made from it, and gives them colliders; the demos build level collision by hand.

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)): its level is an authored model of platforms, cliffs and ramps, and every piece must be solid.

## Does anything plan it?

No, and it was ruled out once: a collision asset pipeline is out of scope on the physics map (`.scratch/fenrir-physics/map.md`, Out of scope). This issue asks whether a smaller piece belongs in the engine: a helper that turns a loaded mesh's vertices into a collision shape, with no new asset type.

## Does it block the game?

Yes, if the level is an authored model, and the game is C#. Checked 2026-11-19: C# can call `PhysicsWorld.CreateCollisionMesh` with vertices it is given, but no asset binding returns a loaded mesh's positions (only `GetMeshLodIndices`), so a C# game cannot make a loaded model solid. The game's rule is that such a reach gap is fixed in the engine, not in its C++ host. How levels are authored is not yet decided ([How are levels authored and loaded?](../../flightless-frenzy/issues/10-how-levels-are-authored-and-loaded.md) waits on the engine's levels and scenes design), and that design may answer this.

## Comments

- 2026-11-19, charting [Engine gaps for Flightless Frenzy](../map.md): settled by [Does a loaded model give C# its collision?](28-does-a-loaded-model-give-csharp-its-collision.md).
- 2026-11-19, round 1 of charting [Engine gaps for Flightless Frenzy](../map.md): the C# reach half is spec-ready in [Does C# get a loaded mesh's positions?](33-does-csharp-get-a-loaded-meshs-positions.md); the model-to-collider helper waits in [Does a loaded model give C# its collision?](28-does-a-loaded-model-give-csharp-its-collision.md).
- 2026-11-19: the spec is written, [C# reach: parenting, scenes, the app, C# resources and mesh positions](../../fenrir-scripting-reach/spec.md) (the mesh positions half), `ready-for-agent` for `/to-tickets`.
- 2026-11-19: tickets cut, `.scratch/fenrir-scripting-reach/issues/01` to `06` (stack 02, 03, 05, 06; 01 and 04 beside).
