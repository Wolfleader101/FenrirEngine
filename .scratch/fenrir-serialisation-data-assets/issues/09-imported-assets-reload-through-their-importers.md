# 09: Imported assets and shader containers reload through their importers

**What to build:** the watcher from ticket 08 covers every file the store loaded, not just data assets (ADR-0081). A changed model, image, cubemap or shader container is re-imported and replaced under its handles.

- **Watched files:** model files (glTF, GLB, FBX, OBJ), images, `.cubemap` files and shader containers each get a watcher.
- **On a change:**
  - The file's own importer runs into private data.
  - The results are `Replace`d under the existing handles: a model and every one of its sub-assets under their labels, and an image or a shader container as its one asset.
  - Revisions bump, and `Reloaded()` lists each swap.
- **On failure:** a failed import keeps the old data, with the importer's message logged once per change. A sub-asset the new file no longer has keeps its old data, with one warning.

**Blocked by:** 08.

**Touches:** `packages/FenrirAssets/` (`src/AssetWatcher.cpp`, `src/AssetStore.cpp`, `src/ModelLoading.cpp`, `src/CubemapLoading.cpp`, `Tests/HotReload.cpp`, fixtures copied into a temporary root)

**Status:** ready-for-agent

- [ ] A rewritten image reloads under its handle within two frames with both intervals at zero, and `Reloaded()` names it.
- [ ] A rewritten glTF replaces its model and its meshes under their labels, and existing handles read the new data.
- [ ] A shader container and a `.cubemap` reload the same way.
- [ ] A broken rewrite of each kind keeps the old data and logs once.
