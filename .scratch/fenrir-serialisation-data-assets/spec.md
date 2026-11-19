Status: ready-for-agent

# FenrirAssets data assets: `AssetKind::Data` in the store, the envelope, typed references, the move tool, the asset source seam and in-place hot reload

Origin: the serialisation map (`.scratch/fenrir-serialisation/map.md`), gathering tickets 10 (what a data asset is), 11 (asset identity and references), 15 (the store's doors), 17 (hot reload) and 20 (the asset source seam), with the as-is of ticket 08. Settled in grilling sessions on 2026-11-19. It is the third of the map's six specs: it builds on the format spec (`.scratch/fenrir-serialisation-format/spec.md`, which builds on the types spec) and cites rather than restates them.

| ADR | Decision |
|---|---|
| 0061 | A data asset is a game-defined plain struct in the asset store, with single inheritance and polymorphism through references |
| 0062 | An asset's identity in files is its root-relative path, and a tool rewrites references when a file moves |
| 0065 | FenrirSerialisation bridges described values and JSON5 trees, and FenrirAssets loads and saves scenes and data assets |
| 0067 | The asset store watches every loaded file, replaces data assets in place, and patches running scenes and instances by entity id |
| 0071 | The development formats promise a cooked form path identity, compiled-in descriptions, the tree encoding and one asset source seam |
| 0063 | Files are JSON5 in one envelope, read and written by an engine-owned parser, and migrated by per-type upgraders |
| 0021 | FenrirAssets owns plain-data assets behind generational handles |
| 0024 | FenrirRenderer's materials are shared assets, bound through a reflected material set |
| 0031 | Skeleton poses live in FenrirSkeleton and models import through cgltf and ufbx into one bind space |

It implements the data asset parts of ADR-0065, 0067 and 0071 (the scene parts are the scenes spec's), and amends ADR-0021's "absolute paths are taken as given".

## Prerequisite

The types spec and the format spec are built and merged: `FenrirTypes` holds the type registry with the `data-asset` role, each type's base chain and canonical type name, the kinds `string`, `list`, `optional`, `reference` and `fixed-string`, and `tools/generate-type-registry`; `FenrirSerialisation` holds the value tree, the JSON5 reader and writer, the bridge over the registry's visitor with its hook for `reference` fields, upgraders, located diagnostics and `ReadTree`. The integration branch `fenrir-serialisation-data-assets` is cut from the format spec's once it lands. Ticket 07 touches only `FenrirCore` and `FenrirScripting` and needs neither.

## Problem Statement

The store holds nine kinds of asset (`AssetKind`, `AssetKindCount` is 9), each behind `AssetHandle<T>` typed by a short name whose `AssetTraits<T>` is specialised by hand. `Load<T>(path, owner)` resolves a relative path against `AssetsConfig::root`, takes an absolute path as given, names a sub-asset as `file#Label` and a built-in as `builtin:`, deduplicates by path per kind, and blocks on the main thread; `LoadResult<T>` carries a handle, one optional `LoadDiagnostic` (path, `LoadFailure`, importer message) and `failedUnits`, so `Status()` is `Succeeded`, `Partial` or `Failed` (ADR-0031). Owners are the app or a scene, uncounted; the revision starts at 1 and moves on each `Replace`, material set and `Reset`. `Add<T>(data, owner)` takes no name, so a code-made asset has no path; the path is private, with no query from a handle; the only edit door is the private `EditMaterial(handle, action)`, and the clip and bone mask edits each have their own. Every asset data type holds vectors or strings, so none is describable; `MaterialData`, instance lists, bone masks and render-target images are made only in code, and nothing the engine reads is a Fenrir-authored text file beyond `.cubemap`.

Nothing reads game-defined data from a file. The engine has no asset kind for a plain struct, no envelope reader, no way to write an asset, no rule refusing `..` or an absolute path in a file, no check that a path's case matches the disk, and no tool to move a file without breaking what names it. `Assets::Install` adds no systems, and the store reads files directly: there is no seam between "the bytes at this path" and the directory that holds them.

The one reload precedent is `Fenrir::Detail::ReloadWatcher` in `FenrirScripting`'s private sources: a clockless, I/O-free decision over a stamp of modification time and size, looked at every 250 ms, settled for 400 ms, then "can be opened", answering `Idle`, `Settling` or `Ready`; `GenerationLifecycle` polls it from a sequential system at `SchedulePriority::PreUpdate` when scripting's `hotReload` is on, loads a private copy and swaps all or nothing. It is tested with a fake clock (`ReloadWatcherTests.cpp`). No file the store loads is watched.

## Solution

All in `FenrirAssets`, which gains a public dependency on `FenrirSerialisation` (and through it `FenrirTypes`); `ReloadWatcher` moves to `FenrirCore`; the move tool's CLI is `tools/move-asset`. Scene files and their doors are the scenes spec's even where they live in `FenrirAssets`.

### The data asset kind

- **`AssetKind::Data`** (ADR-0061): one new kind, `AssetKindCount` 10, one slot table, keyed by asset path, deduplicated, owned by the app or a scene and freed with the last owner (ADR-0021). Each record carries its value's canonical type name; a handle's `T` is checked against it through the registry's base chain, so `AssetHandle<WeaponData>` resolves a `Sword` file.
- **Any registered type is a data asset type:** `Detail::AssetTraits<T>` gains a primary template for every type the registry knows in the `data-asset` role (`AssetData<T>` is `T`, `kind` is `AssetKind::Data`), so a game writes no specialisation; the store reads `T`'s canonical type name from the registry and refuses `Load<T>` of a type it does not know as a data asset with `LoadFailure::NotRegistered`. A data asset is a plain C++ struct with single inheritance and no virtual functions, holds described kinds, handles to assets of any kind and described structs by value, and never an `Entity`; polymorphism is only through references (ADR-0061).
- **The envelope** (ADR-0063): a `.asset.json5` file is one object of `format` (1), `type`, `version`, `data` and `sub`. `FenrirAssets` owns one private envelope reader and writer, over `FenrirSerialisation`'s `ReadTree`, upgraders and bridge: it checks `format`, finds `type` in the registry and its assignability to `T`, runs the type's upgraders from `version`, bridges `data` into a private instance, and takes each entry of `sub` the same way. The scenes spec and the dev-time spec call the same reader and writer for `.scene.json5` and `.project.json5`.
- **Sub-assets:** `sub` maps author-chosen names, unique within the file, to embedded data assets of `type`, `version` and `data` (no `sub` of their own). `file#name` loads one by label sharing the parsed file; loading the top-level asset loads every sub-asset for the same owner, as a model's labelled sub-assets do, and a later load of any of them gives the same handle. Model sub-assets keep their numbered labels (ADR-0062).

### The store's doors

- **`Load<T>(path, owner)`** for any data asset type. `LoadResult<T>` gains a `diagnostics` list of `FenrirSerialisation`'s located diagnostics (file, line, column, message) beside `failedUnits`; `Status()` is `Partial` when either is non-empty and a handle was given, `Failed` when the envelope did not parse or `type` is not assignable to `T` (`LoadFailure::WrongType`). Unknown fields warn and are dropped, a wrong-typed value skips its field, both counted as diagnostics and logged once (ADR-0063).
- **`TryGet<T>(handle)`** gives the one shared read-only instance, null for an empty or stale handle or a record whose type is not assignable to `T`.
- **`Add<T>(value, owner, name)`**: a named add keys the asset by `name`, an asset path under the root, so it is writable and deduplicated like a loaded one; the unnamed `Add<T>(value, owner)` stays for every kind and such an asset writes as an empty handle with one diagnostic (ADR-0062).
- **`Edit<T>(handle, action) -> T*`**: a mutable pointer to the instance, bumping the revision, generalising `EditMaterial`; `action` is the word the log uses when the handle does not resolve. Hot reload, inspector edits and code edits share this one path with `Replace` (ADR-0061).
- **`Replace<T>(handle, value)`** as today, for a data asset requiring the record's type to be exactly `T`.
- **`Save<T>(handle) -> SaveResult`** writes the asset and its sub-assets to its own path through the writer, defaults omitted in declaration order so the same data writes the same bytes (ADR-0063); **`Save<T>(handle, path)`** writes to a new path. Writes are atomic, beside then rename (ADR-0065); the writer refuses a path that is absolute or escapes the root (ADR-0071). `SaveResult` is ADR-0065's: status, located diagnostics and the count of references written empty.
- **`PathOf<T>(handle) -> std::optional<std::string>`**: the public path-from-handle query, the asset path for a loaded or named asset and empty for an unnamed or stale one.

### Identity, references and the asset source

- **The asset path is the identity** (ADR-0062): root-relative, forward slashes, extension included, `#label`, `builtin:`. A path in a file that is absolute or contains `..` is refused with a diagnostic. Comparison is case-sensitive; the directory source warns in a development build when a file is found only by a case-insensitive match. There are no GUIDs, sidecars or id index.
- **References are the path string, typed by the field:** a `reference` field (types spec) declared `AssetHandle<T>` reads a path, and the store checks the target's kind and, for a data asset, its type's assignability to `T`. A missing or wrong-typed target leaves the handle empty with one diagnostic naming file, field, path, expected and found, counted in `diagnostics`.
- **Hard references:** `FenrirAssets` supplies the bridge's reference hook. Each referenced asset loads synchronously with the referencing load's owner before the field is filled; a file's slot is reserved under its path before its references load (as `ReserveBuiltin` does for `builtin:` names), so a reference to a file already loading gets its handle at once and cycles resolve through the store's deduplication (ADR-0062).
- **Writing a reference** uses `PathOf`; an empty handle equals the field's default and is omitted.
- **The `AssetSource` seam** (ADR-0071): a private interface answering "the bytes at this asset path" and "does this path exist", with the directory source over `AssetsConfig::root` as the only implementation; every envelope read goes through it, so a pack source later serves the same doors. The root's layout is no contract beyond relative paths.
- **`Assets::Move(root, from, to, registry) -> MoveResult`** (ADR-0065): needs no running store; moves or renames one file under the root and rewrites every reference to it, and to its sub-assets, across the root's `.json5` envelopes, knowing which strings are asset paths from the registry's `reference` kinds and re-emitting each changed file through the writer so comments and bytes elsewhere survive; `MoveResult` lists each file changed, each left unscanned, and nothing is written on a dry run. **`tools/move-asset <root> <from> <to> [--dry-run]`** is the thin CLI over it with the engine's type registrations.

### Hot reload

- **`ReloadWatcher` moves to `FenrirCore`** as a public header in `Fenrir`, its tests with it, and `FenrirScripting` includes it from there (ADR-0067).
- **One watcher per loaded file of every kind** (data assets, models, images, cubemaps, shader containers), polled between frames on the main thread from a sequential system at `SchedulePriority::PreUpdate` that `Assets::Install` adds when **`AssetsConfig::hotReload`** (true by default, off when cooked) is on, stamps spread across the interval. OS notifications are not used.
- **Data assets reload in place:** the changed file is parsed into a private instance and `Replace`d under the same handle, each sub-asset under its own, bumping revisions; a failure keeps the old instances and logs the located diagnostics once per change. A deleted file keeps its data with one warning and reloads if it reappears.
- **Imported assets reload through their importer** the same way: a model file replaces its model and every sub-asset under their handles, images and shader containers their one asset.
- **`AssetStore::Reloaded()`** lists this frame's swaps, cleared at the next poll, so a game or an editor reacts once.

### Testing

The seam is the store's public interface on the headless `App`, as `AssetsTest.hpp` already has it: `AssetsApp` (an `App` with `Assets::Install` over `Fixtures()`), `RecordingLogger` (errors and warnings as lists) and the `Triangle()` mesh, with `Loading.cpp`, `Owners.cpp` and `Revisions.cpp` as the shape to copy (one behaviour per `TEST_CASE`, `INFO` on each `REQUIRE`). The test package gains a described `WeaponData` extending to `Sword`, with a `string`, a `list`, a nested struct, an `AssetHandle<Image>` and an `AssetHandle<WeaponData>` field, whose type registration the real `tools/generate-type-registry` writes from a test selection (ADR-0065), and `.asset.json5` fixtures: a sword, a sword with embedded sub-assets, a pair referencing each other, a reference to a missing file, a reference to a `.png` from an `AssetHandle<WeaponData>` field, a file at an old `version`, a file with a syntax error. What makes a good test here: load then `Save` reproduces the fixture's bytes (the format research's sword sample is the golden file); `Partial` carries the expected diagnostics with their line and column; owners and deduplication hold for sub-assets and referenced files as they do for a model's; `Edit` and `Replace` move the revision as `Revisions.cpp` checks; `PathOf` answers for loaded, named and unnamed assets. Hot reload tests copy fixtures into a temporary root, rewrite a file between `RunFrame` calls with `reloadInterval` and `reloadDebounce` at zero, and check the same handle reads the new value, `Reloaded()` names it once, and a broken rewrite keeps the old; `ReloadWatcher` keeps its fake-clock tests in `FenrirCore`. `Move` tests run on a temporary root with a dry run first; the CLI gets one CTest smoke run. No window.

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | `AssetKind::Data`, the envelope reader and `Load<T>` of a plain data asset with `diagnostics` and `Partial`, over generated test types | — |
| 02 | The `AssetSource` seam, the directory source, path refusal, the case warning and `PathOf` | 01 |
| 03 | Embedded sub-assets: `sub`, `file#name`, shared parsed file and owners | 01 |
| 04 | Typed references through the bridge's hook: kind and assignability checks, diagnostics, hard loads with the owner, cycles | 02, 03 |
| 05 | Named `Add<T>`, `Edit<T>` generalising `EditMaterial`, and `Replace<T>` for data assets | 01 |
| 06 | `Save<T>` through the envelope writer, atomic, byte-identical against the golden file, with `SaveResult` | 04, 05 |
| 07 | `ReloadWatcher` moves to `FenrirCore` with its tests; `FenrirScripting` uses it from there | — |
| 08 | The asset watcher: `AssetsConfig::hotReload`, data assets replaced in place, `Reloaded()`, the deleted-file warning | 05, 07 |
| 09 | Imported assets and shader containers reload through their importers | 08 |
| 10 | `Assets::Move` with `MoveResult` and the dry run | 06 |
| 11 | `tools/move-asset` CLI with a CTest smoke run | 10 |

Tickets 01 to 06, 08, 09 and 10 touch `packages/FenrirAssets/src/` and `AssetStore.hpp`, so they stack in that order; 11 adds `tools/move-asset` and stacks on 10. Ticket 07 touches only `packages/FenrirCore/` and `packages/FenrirScripting/` and runs beside the stack off the base; it must be in the base before 08 is cut.

## Out of scope

The types spec's registry, roles, kinds and generator, and the format spec's tree, reader, writer, bridge, upgraders, binary tree encoding and `ReadTree`. The scenes spec's files and doors (`LoadScene`, `LoadSceneInto`, `Instantiate`, `SaveScene`, `Unpack`, `CopyScene`, `EntityId`, `Transient`, `UnknownComponents`, `Instance`, the scene patch and instance re-patch). The C# spec's `IDataAsset`, generator, codecs, cache, facade doors and the after-reload re-read of changed C# types (ADR-0067). The dev-time spec's project file, `Project::Load` and import sidecars. The map's fog: soft references, value inheritance between files, the semantic merge driver, the import cache, editing support beyond `Reloaded()`, text and localisation, and Fenrir cooking (the pack source, pre-baked imports, stripping). The first engine-provided data assets (materials, instance lists, bone masks, render-target images) are a follow-up once this spec is built; asynchronous loading stays deferred (ADR-0021); OS change notifications are declined (ADR-0067).

## Gaps settled here

- `Load` of an absolute path under the root is keyed by its root-relative form (as the "same file named two ways" test already expects); one outside the root is refused with a new `LoadFailure::OutsideRoot`, amending ADR-0021's "taken as given" and its test. `LoadFailure` also gains `WrongType` for a file whose `type` is not assignable to `T`.
- `AssetTraits<T>` has a primary template for data asset types rather than generated specialisations; `TryGet<T>` on a `Data` handle checks assignability through the registry and returns null (logged once per handle) otherwise; `Replace<T>` requires the record's type to be exactly `T`, never a base.
- A reference is written in full as `path#name` even to the file's own sub-asset, so the move tool has one rule; a bare `#name` on read is taken as the enclosing file's sub-asset. Sub-assets have no `sub` of their own, and loading a top-level asset loads every sub-asset for the same owner, as models do.
- `Add<T>(value, owner, name)` refuses a name already in use or outside the root with a logged error and an empty handle. `Save<T>(handle, path)` gives an unnamed asset that path as its asset path and leaves a named asset's path unchanged; `Save` of a sub-asset handle writes its whole file, and `Save<T>(handle, path)` on a sub-asset is refused.
- `Edit<T>` is public for data asset types; the material, clip and bone mask edits share its private core; kinds whose `Add` derives data (mesh, skeleton, model) keep `Replace` only.
- `AssetsConfig` gains `reloadInterval` (250 ms) and `reloadDebounce` (400 ms) beside `hotReload`, mirroring scripting's settings, so tests reload within two frames with both at zero and no clock seam is added. `Reloaded()` gives `ReloadedAsset` records (asset path, kind, untyped slot) with `Is<T>(AssetHandle<T>)`. A reference a reload adds loads with every owner of the reloaded asset.
- Only envelope reads go through `AssetSource` now; importers keep opening their files, since a cooked build pre-bakes them and never runs them (ADR-0071); the writer is not part of the seam and writes to the directory root alone.
- `Move` moves or renames one file, refuses an existing destination, rewrites the moved file's own references too, and leaves a file whose `type` the given registry lacks unrewritten, naming it in `MoveResult`.
