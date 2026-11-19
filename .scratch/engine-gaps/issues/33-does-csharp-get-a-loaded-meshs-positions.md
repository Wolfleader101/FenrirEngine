# Does C# get a loaded mesh's positions?

Type: grilling
Status: resolved
Blocked by: none
Settles: [A level model does not give the level its collision](04-collision-from-a-model.md) (the C# reach half)

## Question

C# can cook a collision mesh from vertices it is given but no binding returns a loaded mesh's positions, so C# cannot do what C++ can. Is that reach gap fixed now, apart from whether the engine also offers a model-to-collider helper ([Does a loaded model give C# its collision?](28-does-a-loaded-model-give-csharp-its-collision.md))?

## Answer

Grilled 2026-11-19, round 1 of charting [Engine gaps for Flightless Frenzy](../map.md); the recommendation was accepted.

**Yes, spec-ready, in the C# reach effort.** It is a reach gap and the game's rule is that reach gaps are fixed in the engine, whatever the levels design decides.

- `Assets.GetMeshPositions(AssetHandle<Mesh>, Span<Vector3>)`, returning the count, in the same shape as `GetMeshLodIndices` and `GetMeshLodErrors` (`packages/FenrirAssets/Scripting/src/AssetsScripting.cpp:342,368-385`), with a count query to size the span. It reads the CPU copy `MeshData::positions` the store keeps (`packages/FenrirAssets/include/FenrirAssets/MeshData.hpp:119`).
- Indices already cross through `GetMeshLodIndices` (level 0 is the full mesh), so positions and indices together feed `PhysicsWorld.CreateCollisionMesh`.
- If a later per-asset flag drops the CPU copy after upload (the renderer map's "More asset loading"), the call reports the mesh as unreadable rather than failing silently.
