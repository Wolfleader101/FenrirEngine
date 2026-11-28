---
status: accepted
---

# The asset store watches every loaded file, replaces data assets in place, and patches running scenes and instances by entity id

Editing a data asset or a scene file while the app runs should show in the running app; the level editor research put iteration speed first, and the engine already has a reload precedent: `FenrirScripting`'s `ReloadWatcher`, a clockless decision class (stamp is mtime plus size, looked at every 250 ms, settled 400 ms, must be openable) polled between frames on the main thread, with the assembly loaded from a private copy and replaced all or nothing. The asset store already has `Replace` and a revision per asset that the renderer, animators and skeleton poses check, so a swapped asset reaches its users today. Scene files are authoring data (ADR-0078), instances hold only a diff against their source (ADR-0080), and the engine never writes a scene at game run time, so reload is one direction: disk to app.

We decided that **`FenrirAssets` watches every file it loaded by polling stamps between frames, replaces data assets in place, and patches running scenes and instances by entity id**:

1. **One watcher per loaded file, polled between frames.** `ReloadWatcher` moves from `FenrirScripting`'s private sources into `FenrirCore` so both packages share it. The store keeps one per loaded file of every kind (data assets, scene files it filled, instance sources, imported models, images and shader containers) and polls them from its between-frames step on the main thread, stamps spread across the interval. `AssetsConfig::hotReload` turns it on, true by default as scripting's is; a cooked build turns it off. A deleted file keeps its loaded data with one warning and reloads if it reappears.
2. **Data assets reload in place, all or nothing.** The changed file is parsed into a private instance; on success `Replace` swaps it under the same handle and bumps the revision; on failure the old instance stays and the located diagnostics are logged once per change. `Assets::Reloaded()` gives the handles replaced this frame, cleared next frame. Imported assets reload through their importer the same way.
3. **A changed scene file patches the running scene; it never destroys it.** The store remembers which scene each `LoadScene` filled. Entities in both the file and the scene have their written components rewritten and their parent and order restored; entities only in the file are added; entities whose `EntityId` the file no longer has are destroyed with their children; entities without an `EntityId` and components the file does not write are untouched, but for `Fenrir::Name` and `Fenrir::Transform`, which follow the load rule (ADR-0078 §5): a matched entity whose block no longer lists one loses it; `resources` are rewritten. A parse failure leaves the scene as it was. Whole-scene reload is the caller's `DestroyScene` and `LoadScene`; `LoadSceneInto` merges are not tracked.
4. **An edited source re-patches its instances.** Every instance subtree in every running scene is matched by source id against the new source, each matched entity losing the described components its new block no longer lists, with the host's overrides reapplied on top; run-time children are left alone; the host file is untouched. A model file's instances re-spawn the same way.
5. **A C# reload re-reads what it changed.** Through scripting's after-reload hook, the store re-reads every loaded data asset whose type's shape changed and re-patches scenes' C# components from their files, upgraders running if the version moved. A vanished type's data waits in the file and in `UnknownComponents` until the type returns. A C++ rebuild is a restart.
6. **The engine owns watch, replace and patch.** No external watcher, no editor-only path, no undo; an editor sits on `Reloaded()`, the patch and the diagnostics. Tests drive `ReloadWatcher` with a fake clock and the store with a temporary root rewritten between frames.

## Considered options

- **OS change notifications** (`ReadDirectoryChangesW`, inotify, FSEvents). Fewer `stat`s, but platform code and a thread for a saving a few `stat`s a frame does not need; the poll is the precedent that already works for scripts.
- **Reloading a data asset as a new instance under a new handle.** Holders would never see the change; the store's revision exists for exactly this.
- **Destroy and recreate the scene on change** (Unity's play-mode reload). Loses every run-time entity and the camera the developer is looking through; the patch keeps them and the caller can still choose the two calls.
- **Patching only components whose value changed.** Would preserve run-time edits to file-written fields, but makes the file no longer the truth for what it writes; a designer editing the file expects to see the file.
- **Watching only data assets and scene files.** The mechanism is one `Replace` and the importers already exist; leaving textures and models out would be the odd gap.

## Consequences

- `FenrirCore` gains `ReloadWatcher`; `FenrirAssets` gains per-file watchers, a between-frames poll, `Reloaded()`, the scene patch, the instance re-patch, the C# after-reload re-read and `AssetsConfig::hotReload`.
- A patched scene resets file-written fields a system changed at run time (a body physics moved returns to its file transform); that is the file being the truth, and the next frame's systems continue from there.
- The editing-support fog builds on `Reloaded()` and the patch; a play-in-editor copy of a scene is the level-editor ticket's.
- Cooked builds disable the watcher; the cooked-form ticket decides whether the store even keeps paths there.
