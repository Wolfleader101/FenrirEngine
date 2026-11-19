# 01: Mesh positions from C#

Spec: [C# reach: parenting, scenes, the app, C# resources and mesh positions](../spec.md).

**What to build:** A C# script reads a loaded mesh's positions: `Assets.GetMeshPositions(mesh, span)` copies the mesh's CPU-side positions into a span of `Vector3` and returns how many it wrote, with a count query to size the span, in the shape of `GetMeshLodIndices` and `GetMeshLodErrors`. With the level 0 indices it already reads, a script can cook the mesh's collision through `PhysicsWorld.CreateCollisionMesh`. A mesh whose CPU copy is absent is reported unreadable through the call's result, not as zero positions. Made through the API description and generated bindings, with the description regenerated and committed.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirAssets/Scripting/`, `packages/FenrirAssets/Managed/`, `packages/FenrirAssets/api/`

**Status:** ready-for-agent

Runs off the base, beside the scripting stack: its files are disjoint from 02, 03, 05 and 06, and from 04.

- [ ] An FenrirAssetsScripting test, beside the mesh LOD tests, reads a loaded mesh's positions from C# and checks they match the mesh.
- [ ] The count query sizes the span; a short span is handled as the LOD readers handle it.
- [ ] A mesh with no CPU copy is reported unreadable.
- [ ] The API description is current (the describe-api target leaves it unchanged).
