# 04: `LoadSceneInto` and the subtree `SaveScene` overload

**What to build:** a file can be merged into a running scene, and a selection can be saved as its own file (ADR-0079, ADR-0080; the spec's "Loading and saving").

- **`AssetStore::LoadSceneInto(Scene&, path, Entity parent = {}) -> SceneLoadResult`:** it adds the file's entities under `parent`, or at the top. It ignores the file's `resources`, counting them as skipped, and records nothing for hot reload.
- **`AssetStore::SaveScene(Scene&, Entity root, path) -> SceneSaveResult`:** it writes the subtree under `root` as a scene file. The root block has no `parent`. An `entity` field that leaves the subtree is written `null`, with one diagnostic, and counted.

**Blocked by:** 03.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, `src/SceneReader.cpp`, `src/SceneWriter.cpp`, `Tests/Scenes.cpp`, `fixtures/scenes/`)

**Status:** resolved

- [x] Merging a file under a parent appends its roots under that parent in order, and counts its resources as skipped.
- [x] A subtree saved from a scene equals a committed golden file, with its out-of-subtree references written `null` and counted.
- [x] A subtree saved then merged back reproduces the subtree's components.
