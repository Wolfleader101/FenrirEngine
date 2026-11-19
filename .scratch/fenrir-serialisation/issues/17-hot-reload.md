# How do edited files reload in a running development build?

Type: grilling
Status: resolved
Blocked by: 15

## Question

When a data asset or scene file changes on disk while the app runs, what happens?

- **Watching:** who watches the asset root (the precedent is the script reload watcher in FenrirScripting), on which thread, and whether it is a development-only feature behind a config switch.
- **Data assets:** reload in place (same handle, revision bump, every user sees the new values, as material edits do) or a new instance; what game code that cached values sees; a broken edit (parse error) keeps the old instance and reports.
- **Scene files:** reload the whole scene (destroy and recreate, losing runtime state), patch the running scene (entities matched by file identity, changed components rewritten, added and removed entities applied), or offer both; what happens to entities made at runtime and to components the file does not write.
- **Prefabs:** an edited prefab source and its instances in a running scene.
- **Type changes:** a C++ rebuild is a restart; a C# script reload changing a type's shape while files hold the old shape.
- **Scope:** what is in the engine and what is a tool's.

Zoom into [Who loads and saves files?](15-load-save-api-and-ownership.md) and the reload findings of [What can the engine describe and name today?](08-what-the-engine-can-describe-today.md).

## Answer

ADR: `docs/adr/0067-the-asset-store-watches-every-loaded-file-replaces-data-assets-in-place-and-patches-running-scenes-and-instances-by-entity-id.md`. Glossary: Scene patch, Hot reload. One grilling round, every recommendation accepted.

- **Watching:** `ReloadWatcher` moves from `FenrirScripting`'s private sources to `FenrirCore`; `FenrirAssets` keeps one per loaded file of every kind (data assets, scene files it filled, instance sources, imported models, images and shader containers) and polls them between frames on the main thread, stamps spread across the interval; `AssetsConfig::hotReload`, true by default, off in a cooked build; a deleted file keeps its data with one warning and reloads if it reappears. OS notifications declined.
- **Data assets:** parsed into a private instance and `Replace`d under the same handle with a revision bump, so every holder sees it; a failure keeps the old instance and logs the located diagnostics once per change; `Assets::Reloaded()` lists the handles replaced this frame. Imported assets reload through their importer the same way.
- **Scene files:** the running scene is patched by entity id (components, parent and order rewritten for matched entities, new ones added, dropped ones destroyed with children, run-time entities and unwritten components untouched, resources rewritten); a parse failure changes nothing; whole reload is the caller's `DestroyScene` and `LoadScene`; `LoadSceneInto` merges are not tracked.
- **Prefabs:** every instance of an edited source is re-patched by source id with the host's overrides reapplied, run-time children left alone; model instances re-spawn the same way.
- **Type changes:** after a C# generation reload, the store re-reads data assets of changed types and re-patches scenes' C# components from their files, upgraders running; a vanished type's data waits in the file and in `UnknownComponents`; a C++ rebuild is a restart.
- **Scope:** the engine owns watch, replace and patch; no external watcher, no editor path, no undo; tests use a fake clock and a temporary root rewritten between frames.
- **Left to other tickets:** a play-in-editor copy of a scene (19); whether a cooked store keeps paths (20).
