Status: ready-for-agent

# FenrirAssets scenes: scene files with entity ids, scene instances and variants, the scene doors, and the scene patch

Origin: the serialisation map (`.scratch/fenrir-serialisation/map.md`). The spec gathers six of the map's tickets, settled in grilling sessions on 2026-11-19:
- 13, what a scene file holds;
- 14, prefabs;
- 15, the scene doors;
- 17, the scene patch;
- 19, `CopyScene`;
- the as-is of 08.

It is the fourth of the map's six specs. It builds on the data-assets work, which it cites rather than restates: the envelope, the store's doors, asset paths in fields, the `AssetSource` seam and the asset watcher. That work is built and merged (#461), and its spec is removed. Git history keeps the spec, and the code is in `packages/FenrirAssets/`. On 2026-11-27 this spec was brought in line with the built FenrirTypes, FenrirSerialisation and FenrirAssets. The calls made with the user then are in "Gaps settled here".

| ADR | Decision |
|---|---|
| 0078 | A scene file is authoring data holding described components by entity id and described scene resources, and is read once |
| 0080 | A prefab is any scene or model file instanced by an Instance component, with overrides written as a diff against the source |
| 0079 | FenrirSerialisation bridges described values and JSON5 trees, and FenrirAssets loads and saves scenes and data assets |
| 0081 | The asset store watches every loaded file, replaces data assets in place, and patches running scenes and instances by entity id |
| 0082 | Editor data rides on editor-only components, and brushes, wiring and world data stay outside the engine's scene file |
| 0077 | Files are JSON5 in one envelope, read and written by an engine-owned parser, and migrated by per-type upgraders |
| 0076 | An asset's identity in files is its root-relative path, and a tool rewrites references when a file moves |
| 0085 | The tree encoding, the asset source seam and the cooked form |
| 0083 | The C# side of data assets and scene files (the csharp spec's) |
| 0021 | FenrirAssets owns plain-data assets behind generational handles |

## Prerequisite

All three earlier specs are built and on main. The integration branch `fenrir-serialisation-scenes` is cut from main after them.

**FenrirTypes** (#427):
- The type registry: `TypeRegistry::Find` and `IsA`.
- The `entity` and `fixed-string` kinds, and `@transient` and `@editor-only`.
- Each package's generated type registration.
- `ComponentType::ForName(registry, name)` and `NameOf`, through the `AttachComponent<T>` attachment that a generated registration hangs on each `component`-role type (`packages/FenrirECS/include/FenrirECS/TypeAttachments.hpp`).
- The named scene-resource doors `Scene::FindResource`, `AddResource` and `RemoveResource(registry, name)`, through the `SceneResourceDoors` that `AttachSceneResource<T>` hangs on each `scene-resource`-role type (`packages/FenrirScene/include/FenrirScene/Scene.hpp`).

**FenrirSerialisation** (#444):
- The value tree, with `ReadText` and `WriteText`.
- The bridge: `ToTree`, and `FromTree` returning a `BridgeResult`.
- `Resolvers`, whose `readEntity` and `writeEntity` callbacks carry an `EntityValue{id, list}`.
- Upgraders: `RegisterUpgrader` and `Upgrade`.
- Located diagnostics.
- The tree encoding: `EncodeTree` and `DecodeTree`.

**FenrirAssets data assets** (#461):
- The `.asset.json5` envelope reader and writer (`src/Envelope.*`).
- `Load<T>`, `Save<T>`, and `SaveResult` with `SaveStatus`.
- `PathOf`.
- Asset handle fields, loaded with their owner.
- The `AssetSource` seam with `Spelled`, and every kind refusing a path outside the root.
- `Assets::Move` with `MoveResult`.
- `ReloadWatcher` in `FenrirCore`, and the store's per-file `AssetWatcher` behind `AssetsConfig::hotReload`, with `reloadInterval` and `reloadDebounce`.

## Problem Statement

A `Scene` holds a `std::string` name, an `EntityList` and its scene resources. Nothing writes one to disk:
- every demo scene is a `std::function` spawn in C++;
- `AssetStore::SpawnNodes` is the one rebuild-from-data precedent;
- `ModelNode::sourceId`, stable for one import of one file, is the closest thing to a file-stable identity.

An entity is found only by handle, query or hierarchy walk. Its id is an EnTT slot plus version, and it changes on every run.

Entity links and text:
- Ten component fields across seven types hold an `Entity`, each resolved tolerantly every frame:
  - `Joint`: two;
  - `VehicleWheel`: one;
  - `VehicleDifferential`: two;
  - `VehicleAntiRollBar`: two;
  - `SkinnedMesh`: one;
  - `BoneAttachment`: one;
  - `AnimationLayer`: one.
- Four more sit in read-back state that is not `@transient`: `CharacterBody.state` and `VehicleWheel.state`.
- `Relationship`, a described component, holds four links.
- A world transform is computed (`GetWorldTransform`), never stored.
- `Name` and `BoneName` are `fixed-string` fields.

The ECS already has what a file load needs, and nothing more:
- `ReserveEntity` and `CompleteEntity`, which scripting's change buffer uses;
- `Add`, `Get` and `Has` by `ComponentType`;
- `Watch`, which records adds and removes;
- `AddChild`, which appends a child last, and `RemoveChild`.

Three gaps sit beside it:
- `EntityList::Get` and `Has`, and the scene-resource doors, take a non-const `Scene`.
- `App::CreateScene` checks no duplicate name.
- `AssetStore::SpawnNodes` makes one entity per node under a root named after the file, and FenrirRenderer's `SpawnModel` adds the meshes above it. A model spawned that way can only be saved as its fifty entities.

Missing:
- a file a scene is created from;
- an identity an entity keeps across saves and merges;
- a way to instance one file into another with per-placement overrides;
- the doors that load, save, instantiate and copy;
- a running scene that follows its file when the file is edited.

## Solution

Where things live (ADR-0079):
- The components are in FenrirECS, beside a `@transient` change in FenrirPhysics.
- Everything else is in FenrirAssets.
- FenrirScene's named resource doors are already built.

A scene file is authoring data. It is read when a scene is made and never written by the running game (ADR-0078).

### The components (FenrirECS)

- **`EntityId`:** a described component holding one `u64`, minted once and kept for the entity's life (ADR-0078). Zero means "no id".
  - It is written as a string in lower-case Crockford base32 without padding, as the block's `id`, and never inside `components`.
  - The writer mints five characters (25 random bits, Godot's readable style, `a7k2q`). It re-mints on a collision within the file.
  - A longer id read from a file is kept and written back unchanged.
- **`Transient`:** an empty tag struct.
  - It is undescribed and unregistered. `ComponentType::Native` refuses empty types, and a typed `AddComponent` of one returns no reference, so it is reached only through the typed `HasComponent`, `AddComponent` and `RemoveComponent`.
  - An entity carrying it is not written, nor are its children. The free-fly camera and debug entities carry it.
  - C# cannot add it. It is never in a file, so it needs no name.
- **`UnknownComponents`:** an undescribed per-entity holder of opaque components. For each, it holds:
  - the canonical type name;
  - the type version;
  - the value's subtree, as `EncodeTree` bytes, so FenrirECS links nothing new.

  It is filled at load for a type the registry lacks, with one diagnostic per type per load. It is written back unchanged, and never attached retroactively (ADR-0078).
- **`Instance { source }`:** a described component on a scene instance's root, where `source` is a `fixed-string` asset path (ADR-0080). It is written as the block's `instance`, never inside `components`.
- **Read-back state is `@transient`:** `CharacterBody.state`, `VehicleWheel.state` and `Vehicle.state` are recomputed every step. Each field is marked `@transient`, so their `Entity` fields are never written, and every generated output is regenerated.
  - A component's field marked `@transient` stays in its layout, which bindings mirror, and its description records `transient`: `FieldInfo::transient` at run time. The bridge never writes or reads such a field, and a struct differing from its default only there is left out.
  - A data asset's or settings type's transient field is still left out of its description, as before.
- **Names come from the types work:** the reader and writer reach a component by the name a file spells, through `ComponentType::ForName`, and a scene resource through `Scene::FindResource`, `AddResource` and `RemoveResource`. Scripting attaches C# components at admission (the csharp spec). This spec adds no table.

### The file (FenrirAssets)

- **One scene per file, with no name inside it:** a scene envelope in `<name>.scene.json5`. The caller names the scene, and the name defaults to the asset path.
  - The envelope has the data-asset envelope's keys: `format`, then `type: "Fenrir::Scene"`, `version`, then `data`.
  - Its reader and writer are their own, sharing the `format` check. `Fenrir::Scene` is not a described type, so its layout version is a constant in FenrirAssets, starting at 1.
  - Its `data` holds `resources` then `entities`.
- **`resources`:** an object keyed by canonical type name over every described `scene-resource` type the scene holds (`Fenrir::Sky`, `Fenrir::SkyLight`), with defaults omitted.
  - It is written before `entities` and applied after them, so an `entity` field in a resource resolves.
  - Behaviour resources (`PhysicsWorld`, `SkeletonPoses`, the animators, `DebugDraw`) are undescribed and never written. A package that wants per-scene settings in the file adds a described settings resource beside its behaviour one (ADR-0078).
- **`entities`:** one block per entity, depth-first, with children directly after their parent and siblings in the file's order.
  - A block is `id`, then `parent` (absent on a root), then `instance` (on an instance root), then `components`.
  - `components` is an object keyed by canonical type name. Each entry is the type's fields with defaults omitted, preceded by `version` only when the type's version is above 1; a missing `version` reads as 1. A version-1 component is therefore spelled as the research's scene golden spells it.
  - A `resources` entry is spelled the same way.
  - `Fenrir::Name` and `Fenrir::Transform` come first, then the rest in canonical-name order, so the same scene writes the same bytes (ADR-0077).
  - The omit-at-1 rule is for component and resource entries only. The envelope's own `version` is always written.
- **What is written:** every described `component`-role type the entity has, native or C#.
- **What is not written, and counted:**
  - undescribed components;
  - `Relationship`, which becomes `parent` and the order;
  - `EntityId` and `Instance`, which become `id` and `instance`;
  - `@transient` types, and `@transient` fields (the bridge leaves them out);
  - `UnknownComponents` as a component (its contents are written back as entries);
  - `Transient` entities, with their children.

  `@editor-only` types are written like any other (ADR-0082).
- **`entity` fields** are written through the bridge's `writeEntity`:
  - a target in the same file is its id string;
  - a target inside an instance in the same file is its slash path;
  - a target outside the file is written `null`, with one diagnostic at save (ADR-0078);
  - an empty handle is the field's default: left out, with no diagnostic, so the many fields left unset never warn (the user, 2026-11-27).
- **`fixed-string` fields** (`Name`, `BoneName`, `Instance::source`) are string literals. One read back longer than the field is cut by the bridge at N-1 bytes on a UTF-8 boundary, with its diagnostic.
- **Asset handle fields** are asset paths. They are written through `PathOf`, and loaded with the filled scene as owner, as data assets' are (ADR-0076).
- **Reading through the source:** scene files are read through the store's `AssetSource`, as envelopes are, so a cooked build serves them the same way (ADR-0085).

### Loading and saving

The doors are members of `AssetStore`, beside `Load<T>` and `Save<T>`.

- **Two passes inside one frame** (ADR-0078):
  - Pass one reserves every block's entity (`ReserveEntity`) and builds the id map.
  - Pass two walks the blocks in file order. For each block it:
    - completes the entity;
    - adds and fills its components through the bridge;
    - resolves `entity` fields through the map with `readEntity` (an unknown id leaves the handle empty, with one diagnostic, and is counted);
    - appends the entity to its parent with `AddChild`, so sibling order is the file's.
  - Watches see one add per component, and there is no event.
- **`AssetStore::LoadScene(App&, path, name = path) -> SceneLoadResult`** creates the scene through `App::CreateScene` and fills it. Switching the active scene stays the caller's.
- **`AssetStore::LoadSceneInto(Scene&, path, Entity parent = {}) -> SceneLoadResult`** adds the file's entities under `parent`, or at the top. It ignores the file's `resources` and counts them (ADR-0079).
- Both are synchronous, on the main thread.
- **`SceneLoadResult`:**
  - `status`: `Succeeded`, `Partial`, or `Failed` (the envelope did not parse, named another type, or the path is outside the root);
  - `roots`;
  - counts of entities, components, unknown components kept, fields dropped, references emptied and resources skipped;
  - the located diagnostics, each logged once through the store's logger.
- **`AssetStore::SaveScene(Scene&, path) -> SceneSaveResult`:**
  - mints an `EntityId` for every written entity that lacks one, so the call mutates the scene;
  - checks ids are unique within the file, re-minting a collision;
  - writes ADR-0078's layout;
  - replaces the file atomically, as `Save<T>` does.
- **`AssetStore::SaveScene(Scene&, Entity root, path)`** writes a subtree as its own scene file: "make a prefab from this selection" (ADR-0080). The root block has no `parent`, and `entity` fields that leave the subtree are written `null`.
- **`SceneSaveResult`** is named apart from data assets' `SaveResult`. It holds:
  - `status`;
  - counts of entities and components written, entities and components skipped, and references written `null`;
  - diagnostics.
- Scenes built in code save this way. No dev-loop command ships.
- **`AssetStore::CopyScene(App&, Scene&, name) -> Scene&`** (ADR-0082) runs the writer into a value tree, then the reader out of it into `App::CreateScene(name)`, with no disk. What a file would drop, the copy drops.

### Scene instances

- **One mechanism for scene and model files** (ADR-0080): **`AssetStore::Instantiate(Scene&, path, Entity parent = {}) -> InstanceResult`**:
  - expands the source under `parent`;
  - marks the root with `Instance { source }`;
  - returns `root`, the `SceneLoadResult`, and a lookup from source id to `Entity`.

  The host scene owns what it loaded.
- **A `.scene.json5` source** expands through the scene loader. Its root is the file's single root, or a synthetic root named after the file at the identity transform when it has several.
- **A model source** expands through a `ModelSpawner` seam declared in FenrirAssets, with `AssetStore::SpawnNodes` as the default. FenrirRenderer's `SpawnModel` registers itself as the spawner at install; ADR-0041's `SkinningSource` is the precedent.
  - Its root is `SpawnNodes`' own root, named after the file, with each root node a child of it.
  - `SpawnNodes` marks that root with `Instance`, so `SpawnModel` and `SpawnModelNodes` keep their signatures, and a spawned model saves as one block.
  - The `EntityId`s inside a model instance are the same on every spawn:
    - a node's entity gets its node index plus one;
    - `SpawnModel`'s mesh and skinned-mesh children get the ids after every node's, in spawn order.

    Overrides address them as they address a scene source's ids, a mesh child's material included.
- **An instance block is a reference plus a diff:**
  - `id`, `parent` and `instance: "<path>"`;
  - `components`, merged on top of the source root's (the root `Transform` is the natural override);
  - `overrides: { "<source id>": { components, removed: [type names] } }`;
  - `removedEntities: [source ids]`;
  - added entities as ordinary blocks whose `parent` is a slash path.

  The diff is per field for values, and per component and entity for adds and removes.
- **The writer diffs against a fresh expansion of the source:**
  - For a scene source, that is the source's tree, read, upgraded and expanded, with its own instance blocks expanded the same way, recursively.
  - For a model source, it is the source spawned through the seam into a private `Scene` the writer holds, then written to a tree.
  - The live subtree's tree is compared per source id, component and field. A value equal to the source's is never written, so a source edit reaches every instance that did not override it the next time the host loads.
- **Slash paths:** inner entities are `"<instance id>/<source id>"`, chaining through nested instances.
  - A host field may point in, and an override may point out. A source never names anything outside itself.
  - A file that instances itself, directly or through a chain, is refused at the block with a diagnostic, and counted as a skipped entity.
- **A variant** is a scene file whose single root block is an instance of another scene file, with its own `resources`. There is no `inherits` key, and `LoadScene` loads a variant like any scene.
- **`AssetStore::Unpack(Scene&, Entity root)`** removes the root's `Instance`, one level at a time. It re-mints the ids of the entities it unlinks; those under nested instances keep theirs. The subtree then saves verbatim, and no id repeats in the file.

### Moving a file that scenes name

- **`Assets::Move` scans `.scene.json5` files too** (ADR-0076):
  - each block's `instance` string;
  - each asset path an asset handle field of a component or resource entry holds, found through the registry's descriptions as for envelopes.

  Each that names the moved file, or one of its sub-assets, is rewritten, comments and every other byte kept.
- A scene file that does not parse is listed in `MoveResult::unscanned`. So is an entry whose type the registry lacks; the file's other entries are still rewritten.
- The `tools/move-asset` CLI needs no change beyond its smoke run gaining a scene that instances the moved file.

### Hot reload: the scene patch and the instance re-patch

- **A scene resource records which file each `LoadScene` filled the scene from**, kept beside the scene's `SceneAssetOwner`. The record dies with its scene, so a patch never meets a destroyed scene, and nothing is checked against `ForEachScene`.
  - The store's `AssetWatcher` gains scene-file watches, keyed by file and outside `AssetKind`, for that file and for every instance source the scene expanded (ADR-0081).
  - `LoadSceneInto` merges are not tracked.
- **A changed scene file patches the running scene by entity id:**
  - matched entities have their written components rewritten, and their parent and order restored (`RemoveChild` then `AddChild`, in file order);
  - entities only in the file are added;
  - entities whose `EntityId` the file no longer has are destroyed, with their children;
  - entities without an `EntityId`, and components the file does not write, are untouched;
  - `resources` are rewritten.

  A parse failure changes nothing, and logs once per change. A whole reload is the caller's `DestroyScene` then `LoadScene`.
- **An edited source re-patches every instance of it in every running scene that recorded it:**
  - Each instance is matched by source id against the new expansion, and the host's overrides are reapplied on top.
  - Run-time children are left alone, and the host file is untouched.
  - A model file's instances re-spawn the same way.
- The C# re-read of C# components from scene files after a reload is the csharp spec's, built over this patch.

### Demos

- The demos' spawn functions and scene lists stay code (the map's Out of scope).
- DemoScaffold tags the free-fly camera `Transient`, and `SpawnModel`'s root carries `Instance` through `SpawnNodes`.
- One smoke check runs under `FENRIR_DEMO_SMOKE`. Each demo scene is saved to a temporary path and loaded back into a second scene, and the two scenes' counts of entities and written components are compared.

### Testing

- **FenrirECS unit tests** for the four components (prior art: `packages/FenrirECS/Tests/ComponentTypes.cpp`, `ComponentWatches.cpp`, `Entities.cpp`):
  - an id minted once and spelled in five characters;
  - `Transient` reached through the typed API;
  - `UnknownComponents` holding an opaque value and giving it back.
- **FenrirAssets tests on the headless `App` fixture**: `AssetsTest::AssetsApp` with its `RecordingLogger` over the fixtures root, as `Tests/SpawnNodes.cpp` uses it.
  - Committed golden scene files under `Tests/fixtures/scenes/`, which a code-built scene must reproduce byte for byte.
  - Round trips: save, load, then save again identically.
  - Every count and diagnostic of `SceneLoadResult` and `SceneSaveResult`, each provoked by a broken fixture: an unknown type, an unknown id, a duplicate id, a self-instance.
  - A `ComponentWatch`, drained after a load, showing one add per component.
  - The test package (`Tests/Package/DataAssetsTestPackage.api.toml`, through the real generator) gains a component with an `entity` field and a described scene resource for these tests.
  - The model sources are `fixtures/models/scene.gltf` and `scene.obj`.
- **Instances:**
  - a host instancing a fixture, overridden, saved and compared with a golden file;
  - the source edited and the host reloaded, showing the edit propagated;
  - a variant loaded;
  - `Unpack` then save, equal to the expanded golden.
- **Move:** a prefab referenced by two hosts and by an asset handle field is moved, and every reference is rewritten with every other byte kept.
- **Hot reload:** over a `TempRoot` with `reloadInterval` and `reloadDebounce` at zero, as `Tests/HotReload.cpp` does. The files are rewritten between `RunFrames`, and the tests assert which entities survived, were added or were destroyed, and that a run-time entity stayed. No window.

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | `EntityId`, `Transient`, `UnknownComponents` and `Instance` in FenrirECS; read-back state `@transient` in FenrirPhysics | — |
| 02 | The scene writer: the scene envelope, `SaveScene`, `SceneSaveResult`, the layout and a golden file | 01 |
| 03 | The scene reader: `LoadScene`, the two-pass load, `SceneLoadResult`, unknown components kept and written back | 02 |
| 04 | `LoadSceneInto` and the subtree `SaveScene` overload | 03 |
| 05 | `Instantiate` for scene and model sources, the `ModelSpawner` seam, `SpawnNodes` marking its root, model ids, slash paths, self-instancing refused | 04 |
| 06 | Overrides: the writer's diff against the source, the reader applying it, variants and `Unpack` | 05 |
| 07 | `CopyScene` in memory | 03 |
| 08 | The scene patch on a changed scene file | 06 |
| 09 | The instance re-patch on a changed source | 08 |
| 10 | DemoScaffold: `Transient` free-fly camera and the save-and-reload smoke check | 06 |
| 11 | `Assets::Move` rewrites the asset paths scene files hold | 06 |

How the tickets stack:
- **The store stack:** tickets 02 to 09 all touch `packages/FenrirAssets/src/` (`AssetStore.hpp` and `AssetStore.cpp`) and its tests. They stack in this order: 02, 03, 04, 05, 06, 07, 08, 09.
- **Ticket 01** touches FenrirECS and FenrirPhysics and their generated outputs, and goes first, off the base.
- **Ticket 05** also touches `packages/FenrirRenderer/src/`, for `SpawnModel`'s registration.
- **Tickets 10 and 11** run beside 07 to 09 once 06 is in the base. Ticket 10 touches `examples/DemoScaffold/` alone. Ticket 11 touches `packages/FenrirAssets/src/Move.cpp`, `Tests/Move.cpp` and the move-asset smoke run.

## Out of scope

- **The types spec's:** the kinds, doc commands, registry and generator.
- **The format spec's:** the tree, reader, writer, bridge, upgraders, diagnostics and tree encoding.
- **The data-assets work's:** the envelope, the store doors, asset paths in fields, path-from-handle, the move tool's data-asset scanning, the `AssetSource` seam, the watcher and in-place replace.
- **The csharp spec's:** the facade (`LoadScene`, `Instantiate`, `FindInInstance` and the result structs in C#) and its re-read after a reload.
- **The dev-time spec's:** the project file and the first scene.
- **The map's fog:**
  - game save files, and a convenience over the public blocks;
  - the semantic merge driver;
  - editing support: "apply overrides to source", undo, dirty state;
  - streaming cells;
  - the blockout tool;
  - entity wiring;
  - the editor organisation package;
  - whether the cooker pre-expands instances.
- **The map's out of scope:** editor UI, the demos' scene lists, world-data packages, picking, gizmos and selection.

## Gaps settled here

- **Entry layout:** a component or resource entry, under its canonical-name key, is the type's fields.
  - `version` is written only when the type's version is above 1, and a missing one reads as 1. One upgrader path serves resources and components, and a version-1 component matches the research's scene golden.
  - Entries are ordered `Fenrir::Name`, `Fenrir::Transform`, then canonical-name order.
  - The envelope's own `version` is always written, as a data asset's is.
- **`EntityId`:** one `u64`, spelled in lower-case Crockford base32 without padding.
  - The writer mints five characters (25 random bits), and re-mints an id that collides within the file.
  - A longer id read from a file survives unchanged.
  - `EntityId` and `Instance` are described components that are written as a block's `id` and `instance`, never in `components`.
- **`Transient`** is an undescribed, unregistered empty tag, reached only through the typed API. `ComponentType::Native` refuses empty types, and the user chose this over a one-byte tag or empty-type support on 2026-11-27.
- **`UnknownComponents`** holds each opaque value as `EncodeTree` bytes, so FenrirECS, which links only FenrirMath and FenrirTypes, depends on nothing new.
- **Read-back state:** `CharacterBody.state` and `VehicleWheel.state` become `@transient`, so no scene file writes their `Entity` fields (the user, 2026-11-27). `Vehicle.state`, read-back too, is marked with them. describe-api refused `@transient` on a component's field, so ticket 01 makes such a field stay in the layout and be recorded as `transient`, rather than marking the state types `@transient` (the user chose the long-term option, 2026-11-27).
- **The scene envelope** has its own reader and writer, sharing the data-asset envelope's `format` check, with `type: "Fenrir::Scene"` and a layout version kept as a FenrirAssets constant. `Fenrir::Scene` is not registered (the user, 2026-11-27).
- **The scene doors** are `AssetStore` members. `LoadScene` and `CopyScene` take `App&`, to call `CreateScene`, and the save result is `SceneSaveResult`, named apart from data assets' `SaveResult` (the user, 2026-11-27).
- **Names:** components and scene resources are found by canonical name, through `ComponentType::ForName` and FenrirScene's named resource doors. This spec builds no table of its own.
- **Model sources:**
  - A model source expands through a `ModelSpawner` seam in FenrirAssets. The default is `AssetStore::SpawnNodes`, and FenrirRenderer's `SpawnModel` registers itself at install.
  - The ids inside a model instance are a node's index plus one, with `SpawnModel`'s mesh children numbered after every node, in spawn order (the user, 2026-11-27).
  - The writer's baseline for a model instance is a fresh spawn into a private `Scene`. For a scene source, it is the expanded source tree. Both are compared as trees.
- **`Unpack`** re-mints the ids of the entities it unlinks, so no id repeats in the host file.
- **The patch record:** which file filled a scene is recorded on a scene resource that dies with the scene, rather than validated against `ForEachScene`, where a destroyed scene's address could be reused (the user, 2026-11-27).
- **Self-instancing:** a block that instances its own file is counted as a skipped entity in `SceneLoadResult`, beside its diagnostic.
- **Moves:** `Assets::Move` rewrites the asset paths scene files hold, in ticket 11, so moving a prefab never breaks its hosts (the user, 2026-11-27).
