# 02: The scene writer: the scene envelope, `SaveScene`, `SceneSaveResult`, the layout and a golden file

**What to build:** a scene built in code saves to a `.scene.json5` file in ADR-0078's layout, byte for byte the same each time (the spec's "The file", "Loading and saving").

- **The scene envelope writer:**
  - `format`, then `type: "Fenrir::Scene"`, then `version` (a FenrirAssets constant, 1), then `data` holding `resources` then `entities`.
  - It shares the data-asset envelope's `format` handling. `Fenrir::Scene` is not registered.
- **`AssetStore::SaveScene(Scene&, path) -> SceneSaveResult`:**
  - It mints an `EntityId` for every written entity that lacks one, and re-mints a collision within the file.
  - It writes the blocks depth-first: `id`, `parent`, then `components` keyed by canonical type name.
  - It orders `Fenrir::Name` and `Fenrir::Transform` first, then the rest in canonical-name order.
  - It omits each entry's defaults, and writes `version` only above 1.
  - It writes `resources` from the described `scene-resource` types the scene holds, through FenrirScene's named doors.
  - It replaces the file atomically, as `Save<T>` does, and refuses a path outside the root.
- **What is skipped, and counted:**
  - undescribed components;
  - `Relationship`;
  - `EntityId` and `Instance`, which are spelled as `id` and `instance`;
  - `@transient` types (a `@transient` field is left out by the bridge, uncounted);
  - `Transient` entities, with their children.
- **References:**
  - An `entity` field writes through `writeEntity` as the target's id string. An empty handle, or a target outside the file, is written `null`, with one diagnostic, and counted.
  - An asset handle field writes through `PathOf`.
- **`SceneSaveResult`:**
  - `status`;
  - counts of entities and components written, entities and components skipped, and references written `null`;
  - the diagnostics.
- **Test package:** it gains a component with an `entity` field and a described scene resource, through the real generator.

**Blocked by:** 01.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, a new `src/SceneWriter.cpp` and shared envelope code in `src/Envelope.*`, `Tests/` (a new `Scenes.cpp`, `fixtures/scenes/`, `Package/` and its regenerated outputs))

**Status:** ready-for-agent

- [ ] A code-built scene with names, transforms, a test component with an `entity` field, an asset handle field and a `Fenrir::Sky` saves byte for byte equal to a committed golden file.
- [ ] Saving it twice gives identical bytes, and ids minted the first time are kept the second.
- [ ] A `Transient` entity and its children, `Relationship`, and an undescribed component are skipped and counted.
- [ ] An `entity` field pointing outside the scene is written `null`, with one diagnostic, and counted.
