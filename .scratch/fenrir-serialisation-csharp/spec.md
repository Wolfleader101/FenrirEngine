Status: ready-for-agent

# FenrirScripting and the FenrirAssets scripting extension: C# data asset classes over the store's value tree, file kinds in the script report, and the scene doors on the `Assets` facade

Origin: the Fenrir serialisation map (`.scratch/fenrir-serialisation/map.md`). It gathers ticket 16 (C# data assets and scene data), drawing on ticket 08's C# findings, ticket 10's type model and ticket 15's doors, all settled in grilling sessions on 2026-11-19. It is the fifth of the map's six specs.

It builds on the types, format, data-assets and scenes specs. All four are built and on main (#427, #444, #461, #476), and their spec files are removed, so this spec cites the code and the ADRs rather than restating them. It was brought in line with that code on 2026-11-28; the calls made with the user then are in "Gaps settled here".

| ADR | Decision |
|---|---|
| 0083 | C# data assets are classes materialised from the store's value tree, and scenes reach C# through the Assets facade |
| 0074 | Types read from and written to files are described by the API description and registered with FenrirTypes from generated code |
| 0075 | A data asset is a game-defined plain struct in the asset store, with single inheritance and polymorphism through references |
| 0076 | An asset's identity in files is its root-relative path, and a tool rewrites references when a file moves |
| 0077 | Files are JSON5 in one envelope, read and written by an engine-owned parser, and migrated by per-type upgraders |
| 0078 | A scene file is authoring data holding described components by entity id and described scene resources, and is read once |
| 0079 | FenrirSerialisation bridges described values and JSON5 trees, and FenrirAssets loads and saves scenes and data assets |
| 0080 | A prefab is any scene or model file instanced by an Instance component, with overrides written as a diff against the source |
| 0081 | The asset store watches every loaded file, replaces data assets in place, and patches running scenes and instances by entity id |
| 0082 | Editor data rides on editor-only components, and brushes, wiring and world data stay outside the engine's scene file |
| 0085 | The tree encoding, the asset source seam and the cooked form |
| 0007 | C# scripting is ECS-first, not per-entity behaviour objects |
| 0009 | Engine packages extend scripting through scripting extensions |
| 0029 | Scripting extensions carry native resources and deferred structural changes |
| 0042 | Packages share plain data through a generated, language-neutral API description |
| 0043 | Native resources and facades move onto the generated API pipeline |
| 0044 | Natives pass enums, text and wrappers by value, and bindings use other bindings' mirrors |
| 0046 | A scripting extension's shape is generated and shared: a package writes its natives and its state |

## Prerequisite

**What is built.**
- **FenrirTypes:**
  - `TypeRegistry` (`Register`, `Find`, `Types`, `IsA`, `Attach`);
  - `TypeDescription`, with its static `construct`, `destroy` and `visit` function pointers;
  - `FieldInfo`, with field-level `transient`;
  - the kinds in `Kind.hpp`, which include `string`, `list`, `optional`, `reference`, `entity`, `fixed-string` and `path`;
  - `@version`, `@formerly`, `@transient`, `@editor-only` and the metadata vocabulary.
- **FenrirSerialisation:**
  - the value tree, and the bridge (`ToTree`, `FromTree`, `HeldFieldsOf`);
  - upgraders: `RegisterUpgrader` and the `UpgradersTag` attachment;
  - `EncodeTree`/`DecodeTree` and `ReadTree` sniffing (ADR-0085).
- **FenrirAssets:**
  - `AssetKind::Data` (`AssetKindCount` 11, with Font);
  - the envelope;
  - `Load<T>`, `TryGet`, named `Add`, `Edit`, `Replace`, `Save<T>`, typed references and in-place reload.
  - The scene doors on `AssetStore`, which take `App&` or `Scene&`:
    - `LoadScene`, `LoadSceneInto` and `Instantiate`;
    - both `SaveScene` overloads, and `Unpack`;
    - `CopyScene`.
  - The scene door results `SceneLoadResult`, `SceneSaveResult` and `InstanceResult`, and `Fenrir::Instance` and `EntityId`.
- **FenrirScripting:** the `ComponentTable` and `EventTable`, `ScriptComponents.Report<T>`, `ScriptReport`, and `ScriptData::Admit` and `Forget`. These are all or nothing.

**What must land first.**
- **The scripting-reach spec** (`.scratch/fenrir-scripting-reach/spec.md`, integration branch `fenrir-scripting-reach`): its `Scenes` facade must be on main before tickets 10 and 11 start. Its other tickets don't block this spec.

**The build.**
- It is configured with `FENRIR_ENABLE_SCRIPTING` on, as every scripting ticket is.

## Problem Statement

**What a C# component carries today.**
- A C# component is an `unmanaged` `IComponent` struct.
- `ComponentFieldsGenerator` writes a `ComponentTable` that reports each one through `ScriptComponents.Report<T>` as a full name, a size and an alignment. That is all `ScriptReport` (`components`, `events`) carries.
- Iris reads field names, kinds and offsets through its `Fields` type query (`TypeQueries.cs`), and only for the reload fingerprint. The ECS stores the component as zeroed opaque pages.
- The generator skips fixed buffers and never computes offsets.
- Nothing can tell a scene writer which field is an `Entity`, which is an `AssetHandle<T>` and of what, or that `Name`'s `fixed byte Text[256]` (or an `[InlineArray]` fixed string such as `Instance.Source`) is text.
- A C# class holding a `string` or a `List<T>` cannot be described at all. As a component it is refused at compile time (`FENRIR0004`).
- Admission keeps a report all or nothing. It registers nothing with FenrirTypes, and `ScriptData` holds no `TypeRegistry`.

**What the type registry and the store can hold.**
- The type registry holds static, immutable descriptions. A type with an owning-kind field and no `visit` is refused at `Register`, and no type can be removed.
- The store holds every data asset as a `DataValue`: a type pointer and `description.size` bytes from `Construct`.
- The store's by-name doors (`LoadDataSlot`, `AddDataSlot`, `ReplaceData`, `FindData`, `SaveData`) are private.
- So the store has nowhere to keep a C# data asset, whose layout C++ cannot know.

**The `Fenrir.Assets` facade.**
- It has a hand-written part, `Load<T>`, `Unload` and `IsValid`, over `AssetKind<T>`. That maps nine marker classes onto the 11-member `AssetKind`; Font and Data have no marker.
- It also has generated parts, which Iris.Bindings writes from `@facade` marks.
- Every `AssetHandle<T>` is a bound type listed in `[bound-types]`, and crosses by pointer.
- There is no scene door from C#.
- Iris.Bindings mirrors structs, tagged unions and enums, and writes a defaults test comparing `new T()` with `T{}`. It knows the `data-asset` role and deliberately skips it, emitting no class.

**What survives a script reload.**
- A script reload keeps a component's data when its fingerprint is unchanged, and drops it with a warning otherwise.
- Nothing else C# defines survives, and no C# value is ever read back from a tree.

## Solution

Where each part lives (ADR-0083):
- **FenrirTypes:** the layout-free description kind and removal.
- **FenrirAssets:** the tree-backed record and the public by-name doors.
- **FenrirScripting:** the scripting contract and its generators (`packages/FenrirScripting/Managed/`), and admission in its sources.
- **Iris.Bindings:** the classes for C++ types (`packages/Iris/Tools/Iris.Bindings/`).
- **FenrirAssets' scripting extension:** the natives, the facade and the tests (`packages/FenrirAssets/Scripting/`).

### The registry and the store (FenrirTypes, FenrirAssets)

- **A layout-free description.** A `TypeDescription` may be **tree-backed**. Such a description:
  - has no `construct`, `destroy` or `visit`, and no `size`;
  - has fields that carry names, kinds, targets and metadata, but no offsets;
  - has its default as a value tree.

  `Register` accepts it with owning-kind fields. The registrant owns the description's storage: FenrirScripting keeps an arena per script generation.
  - The bridge and the store treat a tree-backed type as a tree: the type is never constructed, visited or laid out.
  - `IsA` and `base` work as for any data asset type, so a tree-backed type may extend a C++ data asset type.
- **Removal.**
  - `TypeRegistry::Unregister(name)` removes a type and its attachments. It is refused while another registered type names it as its base.
  - FenrirSerialisation gains a matching way to remove a type's upgraders.
- **A tree-backed record.** The store holds a data asset of a tree-backed type as its value tree, not as a `DataValue`.
  - Saving writes that tree, and a reload replaces it.
  - When the type extends a C++ data asset type, the store also keeps a materialised struct of that C++ base, refreshed on every `Replace` and reload. C++ `TryGet<Base>` and `Edit<Base>` read and edit it, and an `Edit` through it is folded back into the tree.
  - `Unregister` of a type that live records still hold is refused by the store's admission check.
- **Public by-name doors.** `AssetStore` gains public untyped doors over the private ones, for scripting and tools:
  - load by type name;
  - the tree of a record, with its revision;
  - replace from a tree;
  - add a named record from a tree;
  - save.

  They take and give trees in the tree encoding. They work for both C++ and tree-backed types: a C++ type is converted through the bridge, and a tree-backed one is kept as given.

### Defining a C# data asset type (`Fenrir.Scripting`)

- **`IDataAsset`** is a marker interface for a `class`. A `struct` implementing it is refused at compile time, with a new generator error.
  - The class extends `object`, another `IDataAsset` class, or the generated class of a C++ data asset type (ADR-0075). Any other base is refused.
  - Its public instance fields and public read-write auto-properties are described. Methods, static members and the rest are not (ADR-0075: "no behaviour").
  - Its canonical type name is its full C# name, such as `MyGame.WeaponData` (ADR-0074).
- **Field kinds** are the `data-asset` role's. Allowed:
  - integers, floats, `bool` and enums;
  - `string`;
  - `List<T>`, as `list`;
  - `T?`, as `optional`;
  - `AssetHandle<T>`, as `reference` with the target's canonical name, or the asset kind for an imported asset;
  - a path, as `path`, through a `[Path]` string;
  - a nested value: a C# `struct` of described fields, or a generated mirror of a described C++ struct, a component type included.

  `Entity` is never allowed (ADR-0075, which describe-api enforces for C++). Also refused, each with a diagnostic naming the field:
  - a class held by value (polymorphism is a reference to a data asset, ADR-0075);
  - a dictionary, an array, an interface or a delegate.
- **Attributes mirror the doc commands** (ADR-0074). Each lives in `Fenrir.Scripting`, with the command's meaning and nothing more:
  - on the type: `[Version(n)]` (1 when absent), `[Transient]`, `[EditorOnly]`;
  - on the type and on fields: `[Formerly("name")]`;
  - `[Upgrader(fromVersion)]` on a `static` method `(TreeNode tree) -> TreeNode`, one per version step;
  - `[Transient]` on a component's field: kept in the layout, never written;
  - `[FixedString]` on a `fixed byte` buffer or an `[InlineArray]` byte struct;
  - the metadata, on fields: `[Range]`, `[Step]`, `[DisplayName]`, `[Group]`, `[ReadOnly]`, `[Hidden]`, `[Multiline]`, `[Units]`.
- **The tree in C#.** `Fenrir.Scripting` gains `TreeNode`, a managed value tree, with a `TreeReader` and a `TreeWriter` over the tree encoding (ADR-0085).
  - They are the one managed consumer of the encoding, written by hand once, and every codec calls them.
  - C# never parses JSON5 and never reads native bytes (ADR-0077, ADR-0083).

### The data asset generator (`Fenrir.Scripting.Generators`)

- **`DataAssetGenerator`** emits, per `IDataAsset` class:
  - its **description entry**: canonical name, version, formerly, metadata, the base's canonical name, fields with kinds and targets, and its default (`new T()` run through the codec into a tree);
  - its two **codecs**, `ToTree(T) -> TreeNode` and `FromTree(TreeNode) -> T`. They walk base fields first (ADR-0075). A field absent from the tree takes the default, and a field the type lacks is dropped under the unknown-field rule (ADR-0077);
  - the registration of its `[Upgrader]` methods by version.
- **Where the codecs go.** They register with `Fenrir.Scripting.DataAssetCodecs`, keyed by `Type`, from a generated `DataAssetTable` beside `ComponentTable` and `EventTable`.
  - Its names go in `GeneratedNames`, and the end-to-end tests pin them equal to FenrirScripting's `GeneratedNames.hpp` (ADR-0042 as amended).
- **Reporting.** `DataAssetTable.Register` reports each entry through `ScriptDataAssets.Report<T>`, which hands the entry to one native as a tree in the tree encoding.
  - Upgraders are not crossed as functions. The table exposes one `Upgrade(name, fromVersion, bytes) -> bytes`, which the engine binds by name as it binds the system host's methods.
  - Admission records which versions each type covers.
- **Diagnostics.** Each is a `FENRIR00xx` error at the member, numbered from the free numbers (0006 to 0009 first):
  - a struct `IDataAsset`;
  - a refused base;
  - a refused field kind;
  - an `Entity` field;
  - `[Upgrader]` on a non-static or mis-typed method;
  - two upgraders for one version;
  - `[FixedString]` on anything but a byte buffer;
  - a generic or inaccessible class (as `FENRIR0002` and `FENRIR0003` for components).

### The script report and admission (FenrirScripting)

- **Components report their fields.** `ComponentTable`'s entry gains a field list:
  - each field's name, kind and target, with `Entity` as `entity`, `AssetHandle<T>` as `reference` with its target, and a `[FixedString]` buffer as `fixed-string`;
  - `[Transient]` fields flagged.

  Rules for building it:
  - Offsets are measured at run time, as `ScriptComponents.AlignmentOf` measures alignment, since the generator computes none.
  - The walk includes fixed buffers and excludes non-public fields.
  - A `string` or `List<T>` stays refused (`FENRIR0004`, ADR-0083).
  - `[Transient]` and `[EditorOnly]` on a component mirror the commands.
- **`ScriptReport` and admission.**
  - `ScriptReport` gains `dataAssets` and per-component fields.
  - `ScriptData` gains the App's `TypeRegistry`.
  - Admission registers every C# component with it, with fields, kinds, offsets and a `new T()` default, as a fixed-layout description in the generation's arena (ADR-0074).
  - It attaches `ComponentType::Dynamic` for each, so the scene writer and reader treat it as a native component.
  - `Forget` unregisters the script's components.
- **Data asset entries are admitted all or nothing** with the rest of the report (glossary *admission*). Every one of these must hold:
  - the canonical name is unique against the registry and the report;
  - the base is a registered data asset type: a C++ one, or a C# one earlier in the report (the table orders bases first);
  - every kind is one the role allows, and every `reference` target is registered or an asset kind;
  - upgrader versions are consecutive below the type's version.

  One failure refuses the whole load and leaves the registry as `Forget` would. Admission registers each type as a tree-backed `data-asset`, with the C# upgraders registered beside it (ADR-0077). `Forget` unregisters both.
- **Reload.**
  - The store's tree is the truth, and it outlives a generation. So after a reload, the next `TryGet` rematerialises from the kept tree under the unknown-field rule (ADR-0083).
  - The generation cache is cleared when the generation stops, so nothing roots the collectible assembly.
  - A shape change unregisters the old type and registers the new one under the same name.
  - The store's re-read after a reload, of changed types from their files with upgraders included, is FenrirAssets' (ADR-0081).

### C++ data asset types in C# (Iris.Bindings)

- **Generated classes.** For every `data-asset` role type in a binding's selection, Iris.Bindings writes `<generated>/<Type>.cs` holding a `public partial class <Type> : IDataAsset`. Iris.Bindings drops its current skip of the role.
  - The class extends its base's class, found through the binding's top-level `uses` key, as mirrors are.
  - It has one property per field: strings as `string`, lists as `List<T>`, optionals as `T?`, references as `AssetHandle<T>`, paths as `string`, and nested structs as their mirrors.
  - Defaults come from the description as initialisers, and docs from the description.
  - It carries the two codecs, and a `[NativeDataAsset("Canonical::Name")]` so `DataAssetGenerator` skips it, as `[NativeComponent]` makes it skip a mirror (ADR-0083).
  - Its codecs register with `DataAssetCodecs` from a module initialiser in the contract.
- **Handles.** `Fenrir::AssetHandle<T>` for a described data asset `T` maps to `AssetHandle<T>`, with no `[bound-types]` entry (ADR-0075).
- **The defaults test** gains data asset types: C++ `T{}` through the bridge into the tree encoding must equal C# `new T()` through its codec, byte for byte.

### The tree natives and the facade (FenrirAssets' scripting extension)

- **Natives** in `AssetsScripting.cpp`, declared and registered as ADR-0046 has it, over the store's public by-name doors:
  - `LoadDataAsset(path, typeName, owner, out handle)` for the store's typed load, with the is-a check (ADR-0075);
  - `DataAssetTree(in handle, knownRevision, Span<byte>, out revision) -> int`, giving the tree in the tree encoding only when the revision moved, with `GetShaderParameter`'s length rule;
  - `ReplaceDataAsset(in handle, ReadOnlySpan<byte>, out revision) -> bool`;
  - `AddDataAsset(typeName, ReadOnlySpan<byte>, owner, name, out handle)`;
  - `SaveDataAsset(in handle)` and `SaveDataAsset(in handle, path)`, as `@facade Assets.Save`.
- **The facade's doors:**
  - **`Assets.Load<T>`** routes a `T` that `DataAssetCodecs` holds to `LoadDataAsset` with its canonical name. The nine marker kinds route as today.
  - **`Assets.TryGet<T>(handle) -> T?`** consults the generation cache, keyed by handle and revision, with one native call per check. On a miss it materialises the value through `FromTree`.
  - **`Assets.Replace<T>(handle, instance) -> bool`** runs `ToTree`, hands the bytes over, and caches the instance under the new revision.
  - **`Assets.Add<T>(instance, owner, name)`** and **`Assets.Save`** complete the doors.
  - All are generic over `where T : class, IDataAsset`. The cache, `TryGet<T>`, `Replace<T>` and `Add<T>` are the hand-written part.
- **No `Edit` from C#** (ADR-0083). The cached instance is one shared view, so a holder that changes it changes every holder's view, and the store sees nothing until `Replace`.
- **Hot reload** is the store's revision bump (ADR-0081), which the cache respects. `Assets.Reloaded()` is not crossed.

### Scenes from C# (the `Assets` facade)

- **Doors named as in C++** (ADR-0079, ADR-0080, ADR-0082), each a `@facade Assets.<Name>` native.
  - The natives reach the App through the native guard's context, and the active scene through the App.
  - They sit beside the scripting-reach spec's `Scenes` facade, which creates, activates and destroys scenes by name.
- **Synchronous doors**, which build a scene that is not active:
  - **`LoadScene(path, name) -> ScriptSceneLoadResult`** creates and fills a scene named `name`. The caller activates it through `Scenes.SetActive(name)`.
  - **`CopyScene(name) -> bool`** copies the active scene under `name`.
- **Saving**, which reads the active scene and changes no structure (ids are minted through the change buffer):
  - **`SaveScene(path)`** and **`SaveScene(root, path)`**, each returning `ScriptSceneSaveResult`.
- **Deferred doors** (ADR-0029). These change the active scene, so they build in a scratch scene and replay through the change buffer, as `Renderer.SpawnModel` does (`RecordCall`):
  - **`LoadSceneInto(path, parent = Entity.None) -> ScriptSceneLoadResult`**.
  - **`Instantiate(path, parent = Entity.None) -> ScriptInstanceResult`**, returning the reserved root.
  - **`Unpack(root) -> bool`**.
- **`FindInInstance(root, sourceId) -> Entity`** gives `Entity.None` when absent. It is a native over a new public `AssetStore::FindInInstance(Scene&, Entity root, id)`, which promotes the internal lookup in `Envelope.hpp`.
- **The result structs** are new flat described shared structs in `ScriptAssets.hpp`, from the binding's own selection (ADR-0043's copy rule). They carry the status enum (`LoadStatus` or `SaveStatus`, described in `FenrirAssetsScripting.api.toml`), the counts and, for `ScriptInstanceResult`, the `root`.
  - Diagnostics are logged once on the C++ side and not crossed.
  - The roots list is not crossed either. A caller that needs the roots parents the load under an entity it made.

### Generated versus hand-written

- **Generated:**
  - the classes and codecs for C++ types;
  - the entries, codecs, table and upgrader registration for C# types;
  - the natives header, declarations, registration, extension maker and facade parts.
- **Hand-written:**
  - `IDataAsset` and the attributes;
  - `TreeNode`, with its reader and writer;
  - `DataAssetCodecs` and the generation cache;
  - the generic `Load<T>` routing, `TryGet<T>`, `Replace<T>` and `Add<T>`;
  - the facade's defaulted-parent overloads;
  - the C++ side: the tree-backed record, the description arena and `Unregister`.
- No per-type hand-written mapping (ADR-0083).

### Testing

The tests use the scripting fixtures' seams, with no window.
- **C++ alone:**
  - FenrirTypes tests register, look up and unregister a tree-backed type. `Unregister` is refused while a registered type names it as its base.
  - FenrirAssets tests hold a hand-registered tree-backed type extending `AssetsTest::WeaponData`, from `Tests/Package/`. They load, replace, add and save it through the public by-name doors, and read and edit its C++ base through `TryGet<WeaponData>` and `Edit<WeaponData>`.
- **Rules tests without .NET:**
  - `Tests/Rules/ScriptDataTests.cpp` admits literal reports with data asset entries: a duplicate name, a missing base, a refused kind and a bad upgrader chain. Each refuses the whole report and leaves the registry unchanged.
  - The FenrirAssets scripting no-op tests (`Scripting/Tests/NoOps.cpp`) call the tree natives through `NativeCallHarness` with a stale handle, an empty span and a mis-typed load.
- **The fixture assembly** (`packages/FenrirAssets/Scripting/Tests/Fixture/`) defines:
  - a C# data asset type extending `AssetsTest::WeaponData`, whose package `AssetsScriptingApp` now registers;
  - a `[FixedString]` component with an `Entity` field and an `AssetHandle<T>` field;
  - upgraders.

  Tests on the headless `AssetsScriptingApp`, over a fixture `.asset.json5`:
  - load the asset and read it from C#;
  - `Replace` it, seen from C++ as a revision bump and a changed tree;
  - `Add`, then `Save`, then `Load`, as a round trip;
  - write back an unchanged `TryGet`–`Replace`–`Save`, byte-identical.
- **Reload.** FenrirAssets' scripting tests gain a `FENRIR_TEST_FIXTURE_V2` build of their fixture, as FenrirScripting's has. The fixture is built again with a changed shape and a moved version. The kept tree must rematerialise, with the dropped field warned and the upgrader run.
- **Scenes.** `LoadScene`, `LoadSceneInto`, `Instantiate` with `FindInInstance`, both `SaveScene`s, `Unpack` and `CopyScene` from C# must give the counts and bytes C++ gives, over FenrirAssets' scene fixtures. A C# component is written by its canonical name, and the deferred doors apply after the change buffer replays.
- **Compile-fail** (`fenrir_expect_compile`):
  - a `struct IDataAsset`;
  - a `string` in a component;
  - an `Entity` in a data asset;
  - a class field by value;
  - a non-static `[Upgrader]`.
- **Defaults.** The generated defaults test covers every data asset class.

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | FenrirTypes: tree-backed descriptions, `TypeRegistry::Unregister`, and upgrader removal | — |
| 02 | FenrirAssets: the tree-backed record, its materialised C++ base, and public by-name doors | 01 |
| 03 | Components report fields (`entity`, `reference`, `fixed-string`, `[FixedString]`, `[Transient]`, `[EditorOnly]`), and admission registers C# components with the registry and `ComponentType::Dynamic` | 01 |
| 04 | `IDataAsset`, the attributes, `TreeNode` with its reader and writer, and `DataAssetGenerator`'s entry, codecs and table, with the compile-fail cases | 03 |
| 05 | The report and admission carry data asset types all or nothing: tree-backed registrations, upgraders bound by name, `Forget` unregistering, rules tests | 04 |
| 06 | Iris.Bindings emits data asset classes and codecs for `data-asset` role types, maps their handles, and extends the defaults test | 04 |
| 07 | The tree natives, `Load<T>` routing, the generation cache, `TryGet<T>`, `Replace<T>`, `Add<T>` and `Save`, with the fixture's C# type extending a C++ one | 02, 05, 06 |
| 08 | A script reload rematerialises from kept trees: FenrirAssets' V2 fixture with a changed shape and a moved version | 07 |
| 09 | The C# scene doors: `LoadScene`, `CopyScene`, both `SaveScene`s, and a deferred `LoadSceneInto`, with the `Script*` result structs | 03 |
| 10 | Deferred `Instantiate` and `Unpack`, `ScriptInstanceResult`, and a public `AssetStore::FindInInstance` with its native | 09 |
| 11 | FenrirScripting's README and Fenrir.Assets' docs, and ScriptingDemo loading a C# data asset and a scene from C# | 08, 10 |

How the tickets stack:
- **01** touches FenrirTypes and FenrirSerialisation, and goes first.
- **02** touches `packages/FenrirAssets/src/` and its tests. It runs beside 03 to 06 once 01 is in.
- **03, 04 and 05** touch `packages/FenrirScripting/Managed/` and `packages/FenrirScripting/src/`, so they stack in order. 08 joins them later.
- **06** touches only `packages/Iris/Tools/Iris.Bindings/`. It runs off 04, beside 05.
- **07, 09 and 10** touch `packages/FenrirAssets/Scripting/`: its `AssetsScripting.cpp`, binding configuration and fixture.
  - 09 and 10 stack off 03, beside 04 to 06. Neither starts until the scripting-reach spec's `Scenes` facade is on main.
  - 07 is cut once 02, 05, 06 and 10 are in the base, and 08 stacks on it.
- **11** is last.

## Out of scope

- **Everything the four specs before it own:**
  - the registry's existing descriptions, kinds and commands;
  - the value tree, bridge, upgraders, tree encoding and `ReadTree`;
  - the envelope, the store's typed doors, references, the move tool, the `AssetSource` seam and hot reload;
  - scene files, `EntityId`, `Instance`, the scene doors in C++ and the scene patch.
- **The dev-time spec's:** the project file, `Project::Load` and import sidecars.
- **The scripting-reach spec's:** the `Scenes` facade, C#-defined scene and app resources, `App.Stop`, parenting and mesh positions.
- **Left out on purpose:**
  - `Edit` from C#;
  - describing C#-defined resources for scene files;
  - a C# `Scene` type;
  - a C# JSON5 reader;
  - crossing diagnostics or root lists to C#;
  - C# upgraders for C++ types;
  - per-entity behaviour objects (ADR-0007).
- **The map's fog:** the cooked form (which may adopt the tree encoding), game save files, soft references, value inheritance between files and editing support.

## Gaps settled here

- **Auto-properties.** Public read-write auto-properties are described beside public fields. The generated classes for C++ types are properties, and a C# class extending one should read the same way; ADR-0075 names fields only.
- **The managed tree.** `TreeNode`, `TreeReader` and `TreeWriter` in `Fenrir.Scripting` are the one hand-written managed consumer of the tree encoding. ADR-0083's hand-written list doesn't name them, but every codec needs one.
- **Defaults and entries.** A C# type's default is `new T()` run through its codec into a tree, carried in the description entry. The entry itself crosses as a tree in the tree encoding through one native, as components report through `Report<T>`.
- **Upgraders.** They are not crossed as function pointers. The generated `DataAssetTable` exposes one `Upgrade(name, fromVersion, bytes)`, bound by name as the system host's methods are, and admission records the versions each type covers.
- **Codec lookup.** It goes through `DataAssetCodecs`, keyed by `Type`, filled by the script's table and by a module initialiser in each contract. Generated classes for C++ types carry `[NativeDataAsset]`, so the scripting generator skips them.
- **`TryGet<T>`'s cost** is one native call per check. `DataAssetTree` takes the known revision and returns bytes only when it moved, so a hit is one crossing with no copy.
- **Where a C# data asset lives** (the user, 2026-11-28). It is a tree-backed record in the store, registered as a layout-free tree-backed description in an arena FenrirScripting owns. FenrirTypes' existing static descriptions are unchanged.
- **Removal** (the user, 2026-11-28). `TypeRegistry::Unregister` and upgrader removal are added, so `Forget` undoes admission and a reload can change a type's shape. The store refuses to drop a type that live records hold.
- **A C# subtype read from C++** (the user, 2026-11-28). The store keeps a materialised struct of a C# type's C++ base, refreshed on `Replace` and reload, so C++ `TryGet<Base>` and `Edit<Base>` keep working. An `Edit` is folded back into the tree.
- **Deferral** (the user, 2026-11-28). `LoadSceneInto`, `Instantiate` and `Unpack` from C# build in a scratch scene and replay through the change buffer, as `Renderer.SpawnModel` does (ADR-0029). `LoadScene` and `CopyScene` stay synchronous, since they build a scene that is not active.
- **Scenes from C#** (the user, 2026-11-28). The scripting-reach spec's `Scenes` facade lands first. `LoadScene` from C# creates a named scene that `Scenes.SetActive` activates, and this spec adds no switching door of its own.
- **`FindInInstance`** (the user, 2026-11-28). It becomes a public `AssetStore` door, rather than the `InstanceResult` map being crossed.
- **Result structs.** These are new flat `ScriptSceneLoadResult`, `ScriptSceneSaveResult` and `ScriptInstanceResult` types, since the C++ results hold vectors, maps and a `Scene*`. They carry status and counts, but not the roots list or the diagnostics.
- **The fixed-string forms.** `[FixedString]` takes both a `fixed byte` buffer and an `[InlineArray]` byte struct, since mirrors use either. Component offsets are measured at run time.
- **Metadata attribute names.** They take the commands' names (`[DisplayName]` for `@display`, `[ReadOnly]` for `@readonly`) and live in `Fenrir.Scripting`, so a script that also imports `System.ComponentModel` qualifies them.
