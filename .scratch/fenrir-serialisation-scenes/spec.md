Status: ready-for-agent

# FenrirAssets scenes: scene files with entity ids, scene instances and variants, the scene doors, and the scene patch

Origin: the serialisation map (`.scratch/fenrir-serialisation/map.md`), gathering tickets 13 (what a scene file holds), 14 (prefabs), 15 (the scene doors), 17 (the scene patch), 19 (`CopyScene`) and the as-is of ticket 08, settled in grilling sessions on 2026-11-19. It is the fourth of the map's six specs and builds on the data-assets spec (`.scratch/fenrir-serialisation-data-assets/spec.md`), which it cites rather than restates: the envelope, the store's doors, asset paths in fields, the `AssetSource` seam and the asset watcher are that spec's.

| ADR | Decision |
|---|---|
| 0078 | A scene file is authoring data holding described components by entity id and described scene resources, and is read once |
| 0080 | A prefab is any scene or model file instanced by an Instance component, with overrides written as a diff against the source |
| 0079 | FenrirSerialisation bridges described values and JSON5 trees, and FenrirAssets loads and saves scenes and data assets |
| 0081 | The asset store watches every loaded file, replaces data assets in place, and patches running scenes and instances by entity id |
| 0082 | Editor data rides on editor-only components, and brushes, wiring and world data stay outside the engine's scene file |
| 0077 | Files are JSON5 in one envelope, read and written by an engine-owned parser, and migrated by per-type upgraders |
| 0076 | An asset's identity in files is its root-relative path, and a tool rewrites references when a file moves |
| 0021 | FenrirAssets owns plain-data assets behind generational handles |

## Prerequisite

The types spec (`fenrir-serialisation-types`: the type registry, the `entity` and `fixed-string` kinds, `@transient` and `@editor-only`, each package's generated type registration), the format spec (`fenrir-serialisation-format`: the value tree, the JSON5 reader and writer, the bridge, upgraders, located diagnostics, the tree encoding) and the data-assets spec (the envelope, `AssetKind::Data`, asset paths in fields checked at load, the path-from-handle query, the `ReloadWatcher` in `FenrirCore` and the store's per-file watchers behind `AssetsConfig::hotReload`) are built and merged. The integration branch `fenrir-serialisation-scenes` is cut after the data-assets spec's last ticket.

## Problem Statement

A `Scene` is a `std::string` name, an `EntityList` and an `unordered_map<type_index, unique_ptr<void>>` of scene resources, and nothing writes one to disk: every demo scene is a `std::function` spawn in C++, `SpawnNodes` is the one rebuild-from-data precedent, and `ModelNode::sourceId` the one file-stable identity in the engine. An entity is found only by handle, query or hierarchy walk; its id is an EnTT slot plus version that changes on every run. Nine component fields hold an `Entity` (joints, wheels, differentials, anti-roll bars, skinned meshes, bone attachments, animation layers), each resolved tolerantly every frame; `Relationship` holds four such links; `WorldTransform` is always computed. The only text in a component is `Name`'s `char[256]`, described today as 256 bytes.

The ECS already has what a file load needs and nothing more: `ReserveEntity` and `CompleteEntity` (used by scripting's change buffer), `Add`, `Get` and `Has` by `ComponentType`, `Watch` recording adds and removes, and `AddChild` appending a child last. `ComponentType` carries no name, a scene resource has no name anywhere in FenrirScene, and `App::CreateScene` checks no duplicate. The model importer's `SpawnNodes` makes one entity per node under a root named after the file, and FenrirRenderer's `SpawnModel` adds the meshes above it; a model spawned that way can only be saved as its fifty entities.

Missing: a file a scene is created from, an identity an entity keeps across saves and merges, a way to instance one file into another with per-placement overrides, the doors that load, save, instantiate and copy, and a running scene that follows its file when the file is edited.

## Solution

Components in FenrirECS, a type-erased resource door in FenrirScene, everything else in FenrirAssets (ADR-0079). A scene file is authoring data, read when a scene is made and never written by the running game (ADR-0078).

### The components (FenrirECS)

- **`EntityId`**: a described component holding one `u64`, minted once and kept for the entity's life (ADR-0078); zero is "no id". Written as a string in lower-case Crockford base32 without padding, the block's first key. The writer mints five characters (25 random bits, Godot's readable style, `a7k2q`), re-minting on a collision within the file; a longer id read from a file is kept and written back unchanged.
- **`Transient`**: an empty tag component. An entity carrying it is not written, nor are its children; the free-fly camera and debug entities carry it. The writer tests for it through the typed `HasComponent`.
- **`UnknownComponents`**: an undescribed per-entity holder of opaque components: for each, the canonical type name, the type version and the value's subtree. Filled at load for a type the registry lacks, one diagnostic per type per load, written back unchanged, never attached retroactively (ADR-0078).
- **`Instance { source }`**: a described component on a scene instance's root, `source` a `fixed-string` asset path (ADR-0080).
- **A name for every component type is the types spec's**: the reader and writer reach a component by the name a file spells through `ComponentType::ForName` and the `ComponentType` attachment each package's generated registration hangs on its `component`-role types, and a scene resource through the type-erased find, add and remove attachment FenrirScene hangs on `scene-resource`-role types (types spec, "Attachments the engine hangs"); scripting attaches C# components at admission (the csharp spec). This spec adds no table.

### The file (FenrirAssets)

- **One scene per file, no name inside it**: a `Fenrir::Scene` envelope (the data-assets spec's) in `<name>.scene.json5`, whose `data` holds `resources` then `entities`. The caller names the scene, defaulting to the asset path.
- **`resources`**: an object keyed by canonical type name over every described `scene-resource` type the scene holds (`Fenrir::Sky`, `Fenrir::SkyLight`), defaults omitted, written before `entities` and applied after them so an `entity` field in a resource resolves. Behaviour resources (`PhysicsWorld`, `SkeletonPoses`, `Animators`, `DebugDraw`) are undescribed and never written; a package wanting per-scene settings in the file adds a described settings resource beside its behaviour one (ADR-0078).
- **`entities`**: one block per entity, depth-first, children directly after their parent, sibling order the file's. A block is `id`, `parent` (absent on a root), `instance` (on an instance root), then `components`: an object keyed by canonical type name, each entry the type's fields with defaults omitted, preceded by `version` only when the type's version is above 1 (a missing `version` reads as 1), so a version-1 component is spelled as the research's scene golden spells it; a `resources` entry is spelled the same way. `Fenrir::Name` and `Fenrir::Transform` come first, the rest in canonical-name order, so the same scene writes the same bytes (ADR-0077).
- **What is written**: every described `component`-role type the entity has, native or C#. Not written, and counted: undescribed components, `Relationship`, `WorldTransform`, `@transient` types, `UnknownComponents` as a component (its contents are written back as entries), and `Transient` entities with their children. `@editor-only` types are written like any other (ADR-0082).
- **`entity` fields** are the target's id string; a target inside an instance in the same file is its slash path; an empty handle or a target outside the file writes `null` with one diagnostic at save (ADR-0078). **`fixed-string` fields** (`Name`, `BoneName`, `Instance::source`) are string literals, read back cut at N-1 bytes on a UTF-8 boundary with a diagnostic. **Asset handle fields** are asset paths, written through the path-from-handle query and loaded with the filled scene as owner, as the data-assets spec has it (ADR-0076).

### Loading and saving

- **Two passes inside one frame** (ADR-0078): pass one reserves every block's entity (`ReserveEntity`) and builds the id map; pass two walks the blocks in file order, completes each entity, adds and fills its components through the bridge, resolves `entity` fields through the map (an unknown id leaves the handle empty, one diagnostic, counted) and appends it to its parent with `AddChild`, so sibling order is the file's. Watches see one add per component; no event.
- **`Assets::LoadScene(path, name = path) -> SceneLoadResult`** creates the scene through `App::CreateScene` and fills it; the active-scene switch stays the caller's. **`Assets::LoadSceneInto(Scene&, path, Entity parent = {}) -> SceneLoadResult`** adds the file's entities under `parent` (or at the top), ignoring the file's `resources` and counting them (ADR-0079). Both synchronous on the main thread.
- **`SceneLoadResult`**: `status` (`Succeeded`, `Partial`, `Failed`: the envelope did not parse or named another type), `roots`, counts of entities, components, unknown components kept, fields dropped, references emptied and resources skipped, and the located diagnostics, each logged once through the store's logger.
- **`Assets::SaveScene(const Scene&, path) -> SaveResult`** mints an `EntityId` for every written entity lacking one (the call mutates the scene), checks ids are unique within the file (re-minting a collision), writes ADR-0078's layout and replaces the file atomically, beside then rename. **`Assets::SaveScene(const Scene&, Entity root, path)`** writes a subtree as its own scene file, the root block without `parent` and `entity` fields leaving the subtree written `null`: "make a prefab from this selection" (ADR-0080). **`SaveResult`**: `status`, counts of entities and components written, entities and components skipped, references written `null`, and diagnostics. Scenes built in code save this way; no dev-loop command ships.
- **`Assets::CopyScene(const Scene&, name) -> Scene&`** (ADR-0082): the writer into a value tree, the reader out of it into `App::CreateScene(name)`, no disk; what a file would drop, the copy drops.

### Scene instances

- **One mechanism for scene and model files** (ADR-0080): **`Assets::Instantiate(Scene&, path, Entity parent = {}) -> InstanceResult`** expands the source under `parent`, marks the root with `Instance { source }` and returns `root`, the `SceneLoadResult` and a lookup from source id to `Entity`. The root is the file's single root, or a synthetic root named after the file at the identity transform when it has several, `SpawnNodes`' rule. The host scene owns what it loaded.
- **A `.scene.json5` source** expands through the scene loader. **A model source** expands through a `ModelSpawner` seam declared in FenrirAssets, `SpawnNodes` by default, which FenrirRenderer's `SpawnModel` registers itself as at install (ADR-0041's `SkinningSource` is the precedent); `SpawnNodes` marks its root with `Instance`, so `SpawnModel` keeps its signature and a spawned model saves as one block. A model instance's inner entities carry `EntityId`s equal to their node index plus one, so overrides address them as a scene source's ids are addressed.
- **An instance block is a reference plus a diff**: `id`, `parent`, `instance: "<path>"`, `components` merged on top of the source root's (the root `Transform` is the natural override), `overrides: { "<source id>": { components, removed: [type names] } }`, `removedEntities: [source ids]`, and added entities as ordinary blocks whose `parent` is a slash path. Per field for values, per component and entity for add and remove.
- **The writer diffs against a fresh expansion of the source**: a scene source's tree, read, upgraded and expanded (its own instance blocks expanded the same way, recursively); a model source spawned through the seam into a private `Scene` the writer holds and written to a tree. The live subtree's tree is compared per source id, component and field, so a value equal to the source's is never written and a source edit reaches every instance that did not override it when the host next loads.
- **Slash paths**: inner entities are `"<instance id>/<source id>"`, chaining through nested instances. A host field may point in, an override may point out, a source never names outside itself. A file that instances itself, directly or through a chain, is refused at the block with a diagnostic and counted as a skipped entity.
- **A variant** is a scene file whose single root block is an instance of another scene file, with its own `resources`; no `inherits` key. `LoadScene` loads it like any scene.
- **`Assets::Unpack(Scene&, Entity root)`** removes the root's `Instance` one level at a time, re-minting the ids of the entities it unlinks (those under nested instances keep theirs), so the subtree saves verbatim and no id repeats in the file.

### Hot reload: the scene patch and the instance re-patch

- **The store remembers which scene each `LoadScene` filled** and keeps a watcher on that file and on every instance source (the data-assets spec's watcher). The record is checked against the App's scenes (`ForEachScene`) before a patch and dropped when the scene is gone; `LoadSceneInto` merges are not tracked (ADR-0081).
- **A changed scene file patches the running scene by entity id**: matched entities have their written components rewritten and their parent and order restored (`RemoveChild` then `AddChild` in file order); entities only in the file are added; entities whose `EntityId` the file no longer has are destroyed with their children; entities without an `EntityId`, and components the file does not write, are untouched; `resources` are rewritten. A parse failure changes nothing. Whole reload is the caller's `DestroyScene` and `LoadScene`.
- **An edited source re-patches every instance** of it in every running scene, matched by source id against the new expansion with the host's overrides reapplied on top; run-time children are left alone; the host file is untouched. A model file's instances re-spawn the same way.
- The C# after-reload re-read of C# components from scene files is the csharp spec's, over this patch.

### Demos

- The demos' spawn functions and scene lists stay code (the map's Out of scope). DemoScaffold tags the free-fly camera `Transient`; `SpawnModel`'s root carries `Instance` through `SpawnNodes`. One smoke check: under `FENRIR_DEMO_SMOKE`, each demo scene is saved to a temporary path and loaded back into a second scene, the two counts of entities and written components compared.

### Testing

- **FenrirECS unit tests** for the four components (prior art: `packages/FenrirECS/Tests/ComponentTypes.cpp`, `ComponentWatches.cpp`, `Entities.cpp`): an id minted once and spelled in five characters, `Transient` reached through the typed API, `UnknownComponents` holding and giving back an opaque value.
- **FenrirAssets tests on the headless `App` fixture** (`AssetsTest::AssetsApp` with its `RecordingLogger` over the fixtures root, as `Tests/SpawnNodes.cpp` uses it): committed golden scene files under `Tests/fixtures/scenes/` that a code-built scene must reproduce byte for byte; round trips (save, load, save again identical); every count and diagnostic of `SceneLoadResult` and `SaveResult` provoked by a broken fixture (unknown type, unknown id, duplicate id, self-instance); a `ComponentWatch` drained after a load showing one add per component. The data-assets spec's test package (its `.api.toml` through the real generator) gains a component with an `entity` field and a described scene resource for these tests; `fixtures/models/scene.gltf` and `scene.obj` are the model sources.
- **Instances**: a host instancing a fixture, overridden, saved and compared with a golden file; the source edited and the host reloaded showing propagation; a variant loaded; `Unpack` then save equals the expanded golden.
- **Hot reload** drives the data-assets spec's fake clock over a temporary root rewritten between frames, asserting which entities survived, were added or destroyed and that a run-time entity stayed. No window.

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | `EntityId`, `Transient`, `UnknownComponents` and `Instance` in FenrirECS, described and registered | — |
| 02 | The scene writer: `SaveScene`, `SaveResult`, the layout and a golden file | 01 |
| 03 | The scene reader: `LoadScene`, the two-pass load, `SceneLoadResult`, unknown components kept and written back | 02 |
| 04 | `LoadSceneInto` and the subtree `SaveScene` overload | 03 |
| 05 | `Instantiate` for scene and model sources, the `ModelSpawner` seam, `SpawnNodes` marking its root, slash paths, self-instancing refused | 04 |
| 06 | Overrides: the writer's diff against the source, the reader applying it, variants and `Unpack` | 05 |
| 07 | `CopyScene` in memory | 03 |
| 08 | The scene patch on a changed scene file | 06 |
| 09 | The instance re-patch on a changed source | 08 |
| 10 | DemoScaffold: `Transient` free-fly camera and the save-and-reload smoke check | 06 |

Tickets 02 to 09 all touch `packages/FenrirAssets/src/` and its tests, so they stack in order: 02, 03, 04, 05, 06, 07, 08, 09. Ticket 01 touches only FenrirECS and its API description and goes first off the base; ticket 05 also touches `packages/FenrirRenderer/src/` for `SpawnModel`'s registration; ticket 10 touches `examples/DemoScaffold/` alone and runs beside 07 to 09 once 06 is in the base.

## Out of scope

The types spec's kinds, doc commands, registry and generator; the format spec's tree, reader, writer, bridge, upgraders, diagnostics and tree encoding; the data-assets spec's envelope, store doors, asset paths in fields, path-from-handle, move tool, `AssetSource` seam, watcher and in-place replace; the csharp spec's facade (`LoadScene`, `Instantiate`, `FindInInstance` and the result structs in C#) and its after-reload re-read; the dev-time spec's project file and first scene. The map's fog: game save files and a convenience over the public blocks, the semantic merge driver, editing support ("apply overrides to source", undo, dirty state), streaming cells, the blockout tool, entity wiring, the editor organisation package, and whether the cooker pre-expands instances. The map's out of scope: editor UI, the demos' scene lists, world-data packages, picking, gizmos and selection.

## Gaps settled here

- A component or resource entry under its canonical-name key is the type's fields, with `version` written only when the type's version is above 1 and read as 1 when missing, so one upgrader path serves top-level values, sub-assets, resources and components and a version-1 component matches the research's scene golden; entries are ordered `Fenrir::Name`, `Fenrir::Transform`, then canonical-name order.
- `EntityId` is one `u64` spelled in lower-case Crockford base32 without padding; the writer mints five characters (25 random bits) and re-mints an id that collides within the file; a longer id read from a file survives unchanged.
- `Transient` is an empty struct reached only through the typed API (`ComponentType::Native` refuses empty types); `UnknownComponents` holds each opaque value in the format spec's tree encoding, so FenrirECS depends on nothing new.
- Components and scene resources are found by canonical name through the types spec's attachments (`ComponentType::ForName`, FenrirScene's type-erased resource door); this spec builds no table of its own.
- A model source expands through a `ModelSpawner` seam in FenrirAssets (default `SpawnNodes`, FenrirRenderer's `SpawnModel` registered at install), and a model instance's inner ids are node index plus one; the writer's baseline for a model instance is a fresh spawn into a private `Scene`, for a scene source the expanded source tree, both compared as trees.
- `Unpack` re-mints the ids of the entities it unlinks so no id repeats in the host file.
- The store validates a remembered scene against the App's scenes before a patch rather than being told when a scene is destroyed.
- A block that instances its own file is counted as a skipped entity in `SceneLoadResult`, beside its diagnostic.
