# How are levels authored and loaded?

Type: grilling
Status: open
Blocked by: 01, 04

## Question

The spec has one level, built so that a second is new data and not new code (settled while charting). What is that data, and who or what writes it?

- Where level geometry is made (a modelling tool exporting a format the engine loads, or built from primitives in a file) and how its collision is made.
- How the things in a level are placed: Kiki's start, cages, coins, power-ups, enemies and their patrols, the exit, the fall boundary, lights and sky. Markers inside the model file, a separate level file, or code.
- How a level is loaded and unloaded, and what a restart reloads.
- How fast the loop from changing a level to playing it is.

The engine/game line applies: a level format that describes *this game's* things is the game's; a general scene file or serialisation the engine lacks is an engine gap (the `fenrir-serialisation` planning, if the inventory finds it, says what is coming). A world editor is out of scope.

## Waiting on the engine's design

Parked 2026-11-19 (the user): the engine's levels and scenes are being designed in another session. Do not grill this ticket until that design is settled; then answer it against what the engine will offer, not against a stopgap.

A first round was asked and not answered. It proposed one folder per level holding a Blender-exported `level.glb` and a `level.json`, things placed as named empty nodes in the model, every mesh solid unless under a `Decor` node, a fall height in the JSON, and a development key that reloads the model. Treat these as options to weigh against the engine's design, not as decisions.

Decided since, in "Where do the assets come from?": the level is the 2022 level's geometry (`levelGeo.obj`, `cliffTrees.obj`, `wallGrass.obj` and its textures), converted to GLB and possibly broken into pieces. This ticket decides how it is broken up. The old level was placed at scale 0.05 and spans about 660 x 680 units in plan.

Facts found for that round (checked in the code, 2026-11-19):

- `SpawnModel` makes one entity per glTF node with its name, transform, meshes, materials and lights; C# can call it and read each entity's `Name`.
- The glTF importer does not read node `extras` (custom properties): nothing in `packages/FenrirAssets` mentions them. A marker can carry data only in its name and its place in the node tree.
- C# can cook a collision mesh from vertices it is given (`PhysicsWorld.CreateCollisionMesh`) and can read a mesh's LOD indices (`GetMeshLodIndices`), but no binding returns a loaded mesh's positions. A C# game cannot make a loaded model solid today.
- C# can unload and load an asset, so a level model can be re-read from disk without asset hot reload.
