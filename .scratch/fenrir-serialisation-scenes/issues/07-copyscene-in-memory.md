# 07: `CopyScene` in memory

**What to build:** a running scene can be duplicated without touching disk, with exactly what a save and load would give (ADR-0082; the spec's "Loading and saving").

- **`AssetStore::CopyScene(App&, Scene&, name) -> Scene&`:**
  - It runs the scene writer into a value tree, then the reader out of it into `App::CreateScene(name)`.
  - It uses no file, and no `AssetSource` read for the scene itself.
  - It records nothing for hot reload.
- What a file would drop, the copy drops: `Transient` entities, undescribed and `@transient` components. Instances copy as instances.

**Blocked by:** 03.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, `src/SceneReader.cpp`, `src/SceneWriter.cpp`, `Tests/Scenes.cpp`)

**Status:** ready-for-agent

- [ ] A copy of ticket 02's code-built scene saves to the same bytes as the original.
- [ ] A `Transient` entity and an undescribed component are not in the copy.
- [ ] No file is written or read for the copy.
