# 05: `Instantiate` for scene and model sources, the `ModelSpawner` seam, model ids, slash paths, self-instancing refused

**What to build:** a scene or model file is placed into another scene as one instance, which saves as one block (ADR-0080; the spec's "Scene instances").

- **`AssetStore::Instantiate(Scene&, path, Entity parent = {}) -> InstanceResult`:** it expands the source under `parent` and marks the root with `Instance { source }`. It returns:
  - `root`;
  - the `SceneLoadResult`;
  - a lookup from source id to `Entity`.
- **A `.scene.json5` source:** its single root, or a synthetic root named after the file at the identity transform when it has several.
- **A model source:** it expands through a new `ModelSpawner` seam in FenrirAssets, with `AssetStore::SpawnNodes` as the default. FenrirRenderer's `SpawnModel` registers itself at install, after ADR-0041's `SkinningSource`.
  - `SpawnNodes` marks its root with `Instance`, and `SpawnModel` keeps its signature.
  - Ids inside a model instance: a node's entity gets its node index plus one, and `SpawnModel`'s mesh and skinned-mesh children get the ids after every node's, in spawn order.
- **The writer:** an instance root writes as `id`, `parent` and `instance: "<path>"`. Its inner entities are not written yet; overrides are ticket 06.
- **Slash paths:** an `entity` field in the host that points into an instance writes, and reads back, as `"<instance id>/<source id>"`, chaining through nested instances.
- **Self-instancing:** a block that instances its own file, directly or through a chain, is refused with a diagnostic and counted as a skipped entity.

**Blocked by:** 04.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, a new `include/FenrirAssets/ModelSpawner.hpp`, `src/SpawnNodes.cpp`, `src/SceneReader.cpp`, `src/SceneWriter.cpp`, `Tests/` (`Scenes.cpp`, `SpawnNodes.cpp`, `fixtures/scenes/`)), `packages/FenrirRenderer/src/` (`SpawnModel`'s registration)

**Status:** ready-for-agent

- [ ] Instancing a scene fixture and `fixtures/models/scene.gltf` puts each under its parent, marked with `Instance`, and each saves as one block.
- [ ] Spawning `scene.gltf` twice gives the same `EntityId`s, nodes and mesh children included.
- [ ] A host field pointing into an instance round-trips as a slash path.
- [ ] A file that instances itself, directly or through another file, is refused with a diagnostic and counted.
