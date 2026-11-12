# FenrirAssets

Plain CPU-side assets behind generational handles, owned by the app or a scene (ADR-0021): meshes, images, materials, shader containers, skeletons, clips, bone masks, instance lists and models, loaded from files or added from code. It has no GPU code; FenrirRenderer and the animation packages each turn its data into their own form. Its importers sit behind a private seam: cgltf reads glTF and GLB, ufbx reads FBX and OBJ (ADR-0031), stb_image reads PNG, JPEG and HDR, and meshoptimizer simplifies meshes into levels of detail (ADR-0057). Each is a submodule under `libs/`, built static and linked `PRIVATE`, so no public header names one.

`Assets::Install(App&, AssetsConfig)` returns the move-only owner of the `AssetStore`. The store's `Load<T>(path, owner)`, `Add<T>(data, owner)`, `Replace(handle, data)` and `Unload(handle)` are the whole interface; every `Add` and `Replace` checks the data and refuses what is malformed with one logged error saying why.

## Levels of detail

A mesh carries its levels of detail as `MeshData::lods`, a vector of `MeshLod { indices, error }`: each a coarser triangle list over the mesh's own vertex streams, with the greatest distance, in local units, by which it departs from full detail. Level 0 is the mesh's own `indices`, with error 0, and is not in the vector. The store refuses a level whose index count is not whole triangles, whose index reaches past the positions or whose error is not greater than the previous level's, and refuses levels on a mesh with more than one submesh.

`Load` takes no import options, so an app that wants levels makes them after loading and replaces the mesh:

```cpp
Fenrir::AssetHandle<Fenrir::Mesh> mesh = store.Load<Fenrir::Mesh>("rock.glb#Rock", owner).handle;
if (const Fenrir::MeshData* data = store.TryGet(mesh))
{
    store.Replace(mesh, Fenrir::GenerateLods(*data));
}
```

`GenerateLods(const MeshData&, const LodSettings&)` returns the mesh with its chain of levels. Each level aims at `LodSettings::ratio` (0.5) of the previous level's triangles, and the chain stops at `maxLevels` (4), when a level would start at or below `minTriangles` (32), or when simplifying saves less than a tenth of the triangles; a cube gets no levels. Where the mesh has normals or `uv0` they weigh in the simplifier's error (`meshopt_simplifyWithAttributes`), and a seam, a position several vertices share, may collapse along its length (meshoptimizer still moves a seam's two sides together, so no crack opens); a mesh with neither goes through `meshopt_simplify`. That is the default because a mesh of many small UV islands, such as a scanned rock, has a seam vertex every few triangles: with every seam vertex locked it stalls at a few hundred triangles, and free it goes on to a few dozen, which LightingDemo's asteroid ring measured as faster with no visible loss. `lockSeams` on locks every seam vertex where it is, so hard edges and UV islands stay exactly where they were, for a mesh whose seams must not move. Each level is simplified from the one before, and its `error` is the sum of each step's error (meshoptimizer's relative error times `meshopt_simplifyScale`), so it is in the mesh's units, bounds the level's departure from full detail and grows strictly. The full-detail indices and each level come back reordered for the GPU's vertex cache; streams, submeshes, bounds and skin are untouched, so a skinned mesh's levels share its skin. A mesh built in code gets its levels the same way before `Add`.

From C# (`Fenrir.Assets`, ADR-0029), a mesh's levels are made and read by handle, since C# never holds a `MeshData` (it adds a mesh as spans, `Assets.AddMesh`): `Assets.GenerateLods(mesh, settings)` replaces the mesh's data with `GenerateLods`' result, `settings` a `LodSettings` mirror whose `new` is C++'s default (`Ratio`, `MaxLevels`, `MinTriangles`, `LockSeams`), and `Assets.GenerateLods(mesh)` uses that default; `Assets.AddMeshLod(mesh, indices, error)` appends a level built by hand under the store's rules (a refused level is logged and changes nothing), and `Assets.ReplaceMesh` drops every level; `Assets.GetMeshLodErrors(mesh, errors)` gives how many levels the mesh has and copies each level's error, and `Assets.GetMeshLodIndices(mesh, level, indices)` a level's triangles, level 0 the mesh's own, as many as fit. `MeshLod` itself has no C# mirror: a level is read as its error and its indices.
