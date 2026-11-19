# Does a loaded model give C# its collision?

Type: grilling
Status: open
Blocked by: none
Settles: [A level model does not give the level its collision](04-collision-from-a-model.md)

## Question

C# can cook a collision mesh from vertices it is given (`PhysicsWorld.CreateCollisionMesh`) but no binding returns a loaded mesh's positions, so a C# game cannot make a loaded model solid. The physics map rules out a collision asset pipeline. Two pieces could close it:

- **A reach fix:** a binding that returns a loaded mesh's positions (and indices), so C# cooks the shape itself, the way C++ can.
- **A helper:** a call that takes a loaded model, or the entities `SpawnModel` made from it, and gives them static colliders, with no new asset type.

Decide which belongs in the engine, by the engine/game line (which meshes are solid is the game's), and whether the answer waits for the engine's levels and scenes design.

**Waits on** the engine's levels and scenes design, being done in another session (the `fenrir-serialisation` map), and on [How are levels authored and loaded?](../../flightless-frenzy/issues/10-how-levels-are-authored-and-loaded.md), parked on it. Take this ticket only once that design says how a level's geometry and collision are authored.

Outcome: a spec-ready design, `wontfix`, or a pointer to the `fenrir-serialisation` map.

## Facts (checked 2026-11-19)

- The CPU copy of a mesh is kept after upload: `AssetStore::TryGet(mesh)` returns `MeshData` with `positions` (`packages/FenrirAssets/include/FenrirAssets/MeshData.hpp:119`), and nothing clears it.
- `GetMeshLodIndices` and `GetMeshLodErrors` copy into a C# span and return a count (`packages/FenrirAssets/Scripting/src/AssetsScripting.cpp:342,368-385`); `Vector3` spans already cross as inputs to `AddMesh` (`Generated/AssetsNative.cs:47`). A positions getter is the same pattern.
- `SpawnModel` gives each node a `MeshRenderer` holding the model's mesh handle (`packages/FenrirRenderer/src/Facade/SpawnModel.cpp:31-63`).
