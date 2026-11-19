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

No. The game can read each mesh's vertices and call `CreateCollisionMesh` itself, if the asset store exposes them to game code (and to C#, should the game be written in it; not checked).

## Comments
