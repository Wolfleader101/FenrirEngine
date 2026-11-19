# Who loads and saves files, and what is the API?

Type: grilling
Status: resolved
Blocked by: 10, 13

## Question

Which package owns reading and writing scene files and data assets, and what does game code call?

- **Package:** FenrirAssets (a scene file and a data asset are assets loaded from the asset root, owned like any other), FenrirScene (a scene knows how to save itself), a new serialisation package both depend on, or a split (the format and registry low, the scene and asset loaders above). What each package may depend on, given FenrirAssets knows no ECS today and FenrirScene knows no files.
- **Loading a scene:** into a new scene, into the active scene additively, or either; whether it is synchronous on the main thread (as asset loads are, ADR-0021) and what async would need; the asset owner of what a scene file loads; the structural change it makes and when systems see it.
- **Saving:** a scene saved from a running app (the dev loop), what is written for entities made in code, and whether saving is engine or tool work.
- **Data assets:** `Assets.Load<T>` returning an `AssetHandle<T>` and a `Get` to the instance, or another door; adding one from code; replacing one (revision bump, as materials do).
- **Diagnostics:** what a failed or partial load reports and through what.
- **Tests:** how load and save are tested without a window (round-trip tests, golden files).

Consult `codebase-design` for the package shape and seams. Zoom into [What is a data asset?](10-what-a-data-asset-is.md) and [What does a scene file hold?](13-what-a-scene-file-holds.md); the assets findings of [What can the engine describe and name today?](08-what-the-engine-can-describe-today.md) give the as-is.

## Answer

ADR: `docs/adr/0065-fenrirserialisation-bridges-described-values-and-json5-trees-and-fenrirassets-loads-and-saves-scenes-and-data-assets.md`. Glossary: FenrirSerialisation, Value tree (and FenrirTypes' avoid line updated). One grilling round, every recommendation accepted; nothing hung off the answers.

- **Package:** three layers. `FenrirTypes` (registry, visitors, no tree); a new `FenrirSerialisation` (value tree with comments as trivia, JSON5 reader and writer, the bridge from a described value to a tree and back through the registry, upgrader application, located diagnostics; depends on `FenrirTypes` and `FenrirLogger` only); `FenrirAssets` (envelope, data asset load and save, scene load and save, the move function; already links `FenrirApp` and `FenrirScene`). A game's save files compose `FenrirSerialisation` alone. One package behind a private seam was declined because the bridge and writer must be public.
- **Loading a scene:** a scene file is instantiated, never a store asset. `Assets::LoadScene(path, name = path)` creates and fills a scene through `App::CreateScene`; `Assets::LoadSceneInto(Scene&, path, parent = {})` adds additively under an optional parent, ignoring the file's resources and counting them. Synchronous on the main thread; referenced assets load with the filled scene as owner; the active-scene switch stays the caller's. `SceneLoadResult` carries status, root entities, counts (entities, components, unknown kept, fields dropped, references emptied, resources skipped) and located diagnostics, each logged once.
- **Saving:** `Assets::SaveScene(const Scene&, path) -> SaveResult`, an engine function that mints missing `EntityId` components, counts skips, names `null` references, and writes atomically to the asset root path; no dev-loop command ships.
- **Data assets:** the store's doors. `Load<T>` (with a `diagnostics` list added to `LoadResult` and `Partial` for tolerant loads), `TryGet<T>`, `Add<T>(value, owner, name)`, `Edit<T>(handle, action)` generalising `EditMaterial`, `Replace<T>`, `Save<T>(handle)` and `Save<T>(handle, path)`; sub-assets by `path#name` share the parsed file.
- **Move tool:** `Assets::Move(root, from, to, registry) -> MoveResult` with a dry run, needing no running store; `tools/move-asset` is the CLI over it.
- **Tests:** `FenrirSerialisation` tests with hand-registered types (round trip, byte identity, comments, upgraders, diagnostic positions) and the research's sword and scene samples as golden files; `FenrirAssets` tests on the headless `App` fixture loading, saving and comparing fixtures; a test package runs the real registration generator. No window.
- **Left to other tickets:** the C# facade over these doors (16); who calls `Replace` and `LoadSceneInto` on a file change (17); asynchronous loading stays deferred (ADR-0021).
