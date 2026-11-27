# 06: Overrides: the writer's diff against the source, the reader applying it, variants and `Unpack`

**What to build:** an instance stores only what differs from its source, so an edit to the source reaches every instance that did not override it (ADR-0080; the spec's "Scene instances").

- **The writer diffs against a fresh expansion of the source:**
  - For a scene source, the baseline is its tree, read, upgraded and expanded recursively.
  - For a model source, it is a spawn through the `ModelSpawner` into a private `Scene`, written to a tree.
  - The writer compares per source id, component and field, and writes:
    - the root's `components` merged on top of the source root's;
    - `overrides: { "<source id>": { components, removed: [type names] } }`;
    - `removedEntities: [source ids]`;
    - added entities as ordinary blocks whose `parent` is a slash path.
- **The reader:** it expands the source, then applies the diff. An override may point out of the instance into the host.
- **A variant:** a scene file whose single root block is an instance of another scene file, with its own `resources`. It loads through `LoadScene`.
- **`AssetStore::Unpack(Scene&, Entity root)`:** it removes the root's `Instance`, one level at a time, and re-mints the ids of the entities it unlinks; those under nested instances keep theirs.

**Blocked by:** 05.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, `src/SceneWriter.cpp`, `src/SceneReader.cpp`, a new `src/SceneInstances.cpp`, `Tests/` (a new `SceneInstances.cpp`, `fixtures/scenes/`))

**Status:** ready-for-agent

- [ ] A host instancing a fixture, with a moved root, an overridden field, a removed component, a removed entity and an added child, saves equal to a golden file, and a value equal to the source's is not written.
- [ ] Editing the source and loading the host again shows the edit on every field the host did not override.
- [ ] A variant loads with its base's entities, its own overrides and its own resources.
- [ ] `Unpack` then save equals the expanded golden, with no id repeated in the file.
