# 08: The scene patch on a changed scene file

**What to build:** a running scene follows its file when the file is edited, matched by entity id (ADR-0081; the spec's "Hot reload").

- **The record:** `LoadScene` puts a scene resource on the scene naming the file it filled it from. It sits beside the scene's `SceneAssetOwner`, and dies with the scene. `LoadSceneInto` and `CopyScene` record nothing.
- **The watch:** the store's `AssetWatcher` gains scene-file watches, keyed by file and outside `AssetKind`, behind `AssetsConfig::hotReload`.
- **On a change**, the store finds every scene whose record names the file, and patches it by `EntityId`:
  - matched entities have their written components rewritten, and their parent and order restored (`RemoveChild` then `AddChild`, in file order);
  - entities only in the file are added;
  - entities whose id the file no longer has are destroyed, with their children;
  - entities without an `EntityId`, and components the file does not write, are untouched;
  - `resources` are rewritten.
- **On failure:** a parse failure changes nothing and logs once per change.

**Blocked by:** 06.

**Touches:** `packages/FenrirAssets/` (`src/AssetWatcher.*`, `src/AssetStore.cpp`, `src/AssetStoreState.hpp`, a new `src/ScenePatch.cpp`, `Tests/` (a new `SceneReload.cpp`))

**Status:** ready-for-agent

- [ ] A rewritten scene file, over a `TempRoot` with both reload intervals at zero, patches the running scene within two frames. A changed field is rewritten, a new entity is added, and a removed one is destroyed with its children.
- [ ] A run-time entity with no `EntityId` survives the patch.
- [ ] A broken rewrite changes nothing and logs once.
- [ ] A destroyed scene is never patched, and its watch is dropped.
