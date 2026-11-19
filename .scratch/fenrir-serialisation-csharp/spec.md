Status: ready-for-agent

# FenrirScripting and the FenrirAssets scripting extension: C# data asset classes over the store's value tree, file kinds in the script report, and the scene doors on the `Assets` facade

Origin: the Fenrir serialisation map (`.scratch/fenrir-serialisation/map.md`), gathering ticket 16 (C# data assets and scene data) over the C# findings of ticket 08 and the type model of ticket 10 and the doors of ticket 15, settled in grilling sessions on 2026-11-19. It is the fifth of the map's six specs and builds on the types spec (`.scratch/fenrir-serialisation-types/spec.md`), the format spec (`.scratch/fenrir-serialisation-format/spec.md`), the data-assets spec (`.scratch/fenrir-serialisation-data-assets/spec.md`) and the scenes spec (`.scratch/fenrir-serialisation-scenes/spec.md`), and cites rather than restates them.

| ADR | Decision |
|---|---|
| 0069 | C# data assets are classes materialised from the store's value tree, and scenes reach C# through the Assets facade |
| 0060 | Types read from and written to files are described by the API description and registered with FenrirTypes from generated code |
| 0061 | A data asset is a game-defined plain struct in the asset store, with single inheritance and polymorphism through references |
| 0063 | Files are JSON5 in one envelope, read and written by an engine-owned parser, and migrated by per-type upgraders |
| 0065 | FenrirSerialisation bridges described values and JSON5 trees, and FenrirAssets loads and saves scenes and data assets |
| 0066 | A prefab is any scene or model file instanced by an Instance component, with overrides written as a diff against the source |
| 0067 | The asset store watches every loaded file, replaces data assets in place, and patches running scenes and instances by entity id |
| 0007 | C# scripting is ECS-first, not per-entity behaviour objects |
| 0009 | Engine packages extend scripting through scripting extensions |
| 0029 | Scripting extensions carry native resources and deferred structural changes |
| 0042 | Packages share plain data through a generated, language-neutral API description |
| 0043 | Native resources and facades move onto the generated API pipeline |
| 0044 | Natives pass enums, text and wrappers by value, and bindings use other bindings' mirrors |
| 0046 | A scripting extension's shape is generated and shared: a package writes its natives and its state |

## Prerequisite

The four specs before it are built: FenrirTypes' type registry with the `data-asset` role, the kinds `string`, `list`, `optional`, `reference`, `entity` and `fixed-string`, `@version`, `@formerly`, `@transient`, `@editor-only` and the metadata vocabulary (types); FenrirSerialisation's value tree, bridge, upgraders and binary tree encoding with `ReadTree` sniffing (format); `AssetKind::Data`, the envelope, `Load<T>`, `TryGet`, named `Add`, `Replace`, `Save<T>`, typed references and in-place reload (data-assets); `LoadScene`, `LoadSceneInto`, `Instantiate`, both `SaveScene` overloads, `Unpack`, `CopyScene`, `SceneLoadResult`, `SaveResult`, `InstanceResult` and `Fenrir::Instance` (scenes). Tickets 01 and 02 need only the types spec; 07 and 08 need the scenes spec. The build is configured with `FENRIR_ENABLE_SCRIPTING` on, as every scripting ticket is.

## Problem Statement

A C# component is an `unmanaged` `IComponent` struct. `Fenrir.Scripting.Generators`' `ComponentFieldsGenerator` writes a `ComponentTable` that reports each one through `ScriptComponents.Report<T>` as a full name, a size and an alignment, and that is all the `ScriptReport` (`components`, `events`) carries: field names, kinds and offsets are read by Iris's `TypeQueries.Fields` for the reload fingerprint alone, and the ECS stores the component as zeroed opaque pages. Nothing can tell a scene writer which field is an `Entity`, which is an `AssetHandle<T>` and of what, or that `Name`'s 256-byte `fixed byte` buffer is text. A C# class holding a `string` or a `List<T>` cannot be described at all: as a component it is refused at compile time (`FENRIR0004`), and the only text that crosses today is a native function's `ReadOnlySpan<char>` parameter. Admission (`ScriptData::Admit`) keeps a report all or nothing and registers nothing with FenrirTypes.

The `Fenrir.Assets` facade is a hand-written part (`Load<T>`, `Unload`, `IsValid` over `AssetKind<T>`, which maps nine sealed never-made classes to `AssetKind`) over the generated parts Iris.Bindings writes from `@facade` marks (`AddMesh`, `ReplaceInstanceList`, `Materials`, `Shaders`). Every `AssetHandle<T>` is a bound type listed by kind in `[bound-types]` and crosses by pointer. There is no scene door from C#, no C# type for a scene, and no door to switch scenes: a script sees the active scene and nothing else. Iris.Bindings mirrors structs, tagged unions and enums and writes a defaults test comparing `new T()` with `T{}`; it knows no `data-asset` role and emits no class.

A script reload keeps a component's data when its fingerprint is unchanged and drops it with a warning otherwise. Nothing C# defines survives in any other form, and no C# value is ever read back from a tree.

## Solution

The scripting contract and its generators in FenrirScripting (`packages/FenrirScripting/Managed/`), admission in FenrirScripting's sources, the classes for C++ types in Iris.Bindings (`packages/Iris/Tools/Iris.Bindings/`), and the natives, facade and tests in FenrirAssets' scripting extension (`packages/FenrirAssets/Scripting/`) (ADR-0069).

### Defining a C# data asset type (`Fenrir.Scripting`)

- **`IDataAsset`** is a marker interface for a `class`; a `struct` implementing it is refused at compile time with a new generator error. The class extends `object`, another `IDataAsset` class, or the generated class of a C++ data asset type (ADR-0061); any other base is refused. Its public instance fields and public read-write auto-properties are described; methods, static members and the rest are not (ADR-0061 "no behaviour"). Its canonical type name is its full C# name (`MyGame.WeaponData`, ADR-0060).
- **Field kinds** are the `data-asset` role's: integers, floats, `bool`, enums, `string` (`string`), `List<T>` (`list`), `T?` (`optional`), `AssetHandle<T>` (`reference` with the target's canonical name, or the asset kind for an imported asset), a C# `struct` of described fields or a generated mirror of a described C++ struct (nested value, a component type included), and `Entity` never (ADR-0061). A class by value, a dictionary, an array, an interface or a delegate is refused with a diagnostic naming the field: polymorphism is a reference to a data asset (ADR-0061).
- **Attributes mirror the doc commands** (ADR-0060): `[Version(n)]` on the type (1 when absent), `[Formerly("name")]` on the type and on fields, `[Upgrader(fromVersion)]` on a `static` method `(TreeNode tree) -> TreeNode` with one per version step, `[FixedString]` on a `fixed byte` buffer, `[Transient]` and `[EditorOnly]` on a type, and the metadata `[Range]`, `[Step]`, `[DisplayName]`, `[Group]`, `[ReadOnly]`, `[Hidden]`, `[Multiline]` and `[Units]` on fields, each in `Fenrir.Scripting` with the command's meaning and nothing more.
- **The tree in C#.** `Fenrir.Scripting` gains `TreeNode`, a managed value tree, and a `TreeReader` and `TreeWriter` over FenrirSerialisation's binary tree encoding: the one managed consumer of the encoding, hand-written once, which every codec calls. C# never parses JSON5 and never reads native bytes (ADR-0063, ADR-0069).

### The data asset generator (`Fenrir.Scripting.Generators`)

- **`DataAssetGenerator`** emits per `IDataAsset` class: its **description entry** (canonical name, version, formerly, metadata, base's canonical name, fields with kinds and targets, and its default: `new T()` run through the codec into a tree); its two **codecs**, `ToTree(T) -> TreeNode` and `FromTree(TreeNode) -> T`, walking base fields first (ADR-0061), a field absent from the tree taking the default and a field the type lacks dropped under the unknown-field rule (ADR-0063); and the registration of its `[Upgrader]` methods by version. The codecs register with `Fenrir.Scripting.DataAssetCodecs`, keyed by `Type`, from a generated `DataAssetTable` beside `ComponentTable` and `EventTable`, its names in `GeneratedNames` and pinned equal to FenrirScripting's `GeneratedNames.hpp` by the end-to-end tests (ADR-0042 as amended).
- **Reporting.** `DataAssetTable.Register` reports each entry through `ScriptDataAssets.Report<T>`, which hands the entry to one native as a tree in the binary tree encoding; upgraders are not crossed as functions: the table exposes one `Upgrade(name, fromVersion, bytes) -> bytes` the engine binds by name as it binds the system host's methods, and admission records which versions each type covers.
- **Diagnostics**, each a `FENRIR00xx` error at the member: a struct `IDataAsset`, a refused base, a refused field kind, an `Entity` field, `[Upgrader]` on a non-static or mis-typed method, two upgraders for one version, `[FixedString]` on anything but a `fixed byte` buffer, a generic or inaccessible class (as `FENRIR0002` and `FENRIR0003` for components).

### The script report and admission (FenrirScripting)

- **Components report their fields.** `ComponentTable`'s entry gains the field list the generator already walks: name, offset, kind and target, with `Entity` as `entity`, `AssetHandle<T>` as `reference` with its target, and a `[FixedString] fixed byte` buffer as `fixed-string`; a `string` or `List<T>` stays refused (`FENRIR0004`, ADR-0069). `[Transient]` and `[EditorOnly]` on a component mirror the commands. `ScriptReport` gains `dataAssets` and per-component fields; admission registers every C# component with FenrirTypes' type registry (fields, kinds, offsets, a `new T()` default, ADR-0060) so the scene writer and reader of the scenes spec treat it as a native one.
- **Data asset entries are admitted all or nothing** with the rest of the report (glossary *admission*): the canonical name is unique against the registry and the report, the base is a registered data asset type (a C++ one, or a C# one earlier in the report, which the table orders bases first), every kind is one the role allows and every `reference` target is registered or an asset kind, and upgrader versions are consecutive below the type's version. One failure refuses the whole load and leaves the registry as `Forget` would. Admission registers each type with the registry as a `data-asset` whose visitor walks a tree, not bytes, with the C# upgraders registered beside it (ADR-0063); `Forget` removes them.
- **Reload.** The store's tree is the truth and outlives a generation, so a reload rematerialises on the next `TryGet` from the kept tree under the unknown-field rule (ADR-0069); the generation cache is cleared at generation stop so nothing roots the collectible assembly. The store's after-reload re-read of changed types from their files, upgraders included, is the data-assets spec's (ADR-0067).

### C++ data asset types in C# (Iris.Bindings)

- For every `data-asset` role type in a binding's selection, `<generated>/<Type>.cs` holds a `public partial class <Type> : IDataAsset` (extending its base's class, found through `[uses]` as mirrors are) with one property per field, strings as `string`, lists as `List<T>`, optionals as `T?`, references as `AssetHandle<T>`, nested structs as their mirrors, defaults from the description as initialisers and docs from the description, the two codecs, and a `[NativeDataAsset("Canonical::Name")]` so `DataAssetGenerator` skips it as `[NativeComponent]` skips a mirror (ADR-0069). Its codecs register with `DataAssetCodecs` from a module initialiser in the contract.
- `Fenrir::AssetHandle<T>` for a described data asset `T` maps to `AssetHandle<T>` with no `[bound-types]` entry (ADR-0061). The binding's defaults test gains data asset types: C++ `T{}` through the bridge into the tree encoding equals C# `new T()` through its codec, byte for byte.

### The tree natives and the facade (FenrirAssets' scripting extension)

- **Natives** in `AssetsScripting.cpp`, declared and registered as ADR-0046 has it: `LoadDataAsset(path, typeName, owner, out handle)` for the store's typed load with the is-a check (ADR-0061); `DataAssetTree(in handle, knownRevision, Span<byte>, out revision) -> int` giving the tree in the binary tree encoding only when the revision moved, with `GetShaderParameter`'s length rule; `ReplaceDataAsset(in handle, ReadOnlySpan<byte>, out revision) -> bool`; `AddDataAsset(typeName, ReadOnlySpan<byte>, owner, name, out handle)`; `SaveDataAsset(in handle)` and `SaveDataAsset(in handle, path)` as `@facade Assets.Save`. The store converts tree to struct for a C++ type and keeps the tree for a C# one (ADR-0069).
- **`Assets.Load<T>`** routes a `T` under `DataAssetCodecs` to `LoadDataAsset` with its canonical name, the nine asset kinds as today. **`Assets.TryGet<T>(handle) -> T?`** consults the generation cache keyed by handle and revision, one native call per check, and on a miss materialises through `FromTree`. **`Assets.Replace<T>(handle, instance) -> bool`** runs `ToTree`, hands the bytes over, and caches the instance under the new revision; **`Assets.Add<T>(instance, owner, name)`** and **`Assets.Save`** complete the doors. There is no `Edit` (ADR-0069): the cached instance is one shared view, so a holder that changes it changes every holder's view and the store sees nothing until `Replace`. All generic over `where T : class, IDataAsset`; the cache, `TryGet<T>`, `Replace<T>` and `Add<T>` are the hand-written part.
- **Hot reload** is the store's revision bump (ADR-0067), which the cache respects; `Assets.Reloaded()` is not crossed.

### Scenes from C# (the `Assets` facade)

- **Doors named as in C++** (ADR-0065, ADR-0066, ADR-0068), each a `@facade Assets.<Name>` native over the active scene, since C# sees one scene: `LoadScene(path, name) -> SceneLoadResult` creates and fills a scene the C++ app activates; `LoadSceneInto(path, parent = Entity.None) -> SceneLoadResult`; `Instantiate(path, parent = Entity.None) -> InstanceResult`; `SaveScene(path) -> SaveResult` for the active scene and `SaveScene(root, path) -> SaveResult` for a subtree; `Unpack(root) -> bool`; `CopyScene(name) -> bool`; `FindInInstance(root, sourceId) -> Entity` (`Entity.None` when absent).
- **`SceneLoadResult`, `SaveResult` and `InstanceResult`** are described shared structs from the binding's own selection (ADR-0043's copy rule): the status enum, the counts and, for `InstanceResult`, the `root`; diagnostics are logged once on the C++ side and not crossed, nor is the roots list (a caller that needs the roots parents the load under an entity it made).

### Generated versus hand-written

Generated: the classes and codecs for C++ types, the entries, codecs, table and upgrader registration for C# types, the natives header, declarations, registration, extension maker and facade parts. Hand-written: `IDataAsset`, the attributes, `TreeNode` with its reader and writer, `DataAssetCodecs`, the generation cache, the generic `Load<T>` routing, `TryGet<T>`, `Replace<T>` and `Add<T>`, and the facade's defaulted-parent overloads. No per-type hand-written mapping (ADR-0069).

### Testing

The scripting fixtures' seams, no window:
- **Rules tests without .NET**: `Tests/Rules/ScriptDataTests.cpp` admits literal reports with data asset entries (duplicate name, missing base, refused kind, bad upgrader chain, each refusing the whole report and leaving nothing); the FenrirAssets rules tests call the tree natives through `NativeCallHarness` with a stale handle, an empty span and a mis-typed load.
- **The fixture assembly** (`packages/FenrirAssets/Scripting/Tests/Fixture/`) defines a C# data asset type extending the data-assets spec's C++ test type, a `[FixedString]` component with an `Entity` and an `AssetHandle<T>` field, and upgraders; tests on the headless `AssetsScriptingApp` over a fixture `.asset.json5`: load and read from C#, `Replace` seen from C++ as a revision bump and a changed tree, `Add` then `Save` then `Load` round trip, and byte-identical write-back of an unchanged `TryGet`–`Replace`–`Save`.
- **Reload** follows `FENRIR_TEST_FIXTURE_V2`: the fixture built again with a changed shape and a moved version, the kept tree rematerialising with the dropped field warned and the upgrader run.
- **Scenes**: `LoadScene`, `LoadSceneInto`, `Instantiate` with `FindInInstance`, both `SaveScene`s, `Unpack` and `CopyScene` from C# give the counts and bytes C++ gives over the scenes spec's fixtures, with a C# component written by canonical name.
- **Compile-fail** (`fenrir_expect_compile`): a `struct IDataAsset`, a `string` in a component, an `Entity` in a data asset, a class field by value, a non-static `[Upgrader]`. The generated defaults test covers every data asset class.

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | Components report fields with the `entity`, `reference` and `fixed-string` kinds, `[FixedString]`, `[Transient]`, `[EditorOnly]`; admission registers C# components with the type registry | — |
| 02 | `IDataAsset`, the attributes, `TreeNode` with its reader and writer, and `DataAssetGenerator`'s entry, codecs and table, with the compile-fail cases | 01 |
| 03 | The report and admission carry data asset types all or nothing: registry entries over trees, upgraders bound by name, rules tests | 02 |
| 04 | Iris.Bindings emits data asset classes and codecs for `data-asset` role types, maps their handles, and extends the defaults test | 02 |
| 05 | The tree natives, `Load<T>` routing, the generation cache, `TryGet<T>`, `Replace<T>`, `Add<T>` and `Save`, with the fixture's C# type extending a C++ one | 03, 04 |
| 06 | A script reload rematerialises from kept trees: the V2 fixture with a changed shape and a moved version | 05 |
| 07 | `LoadScene`, `LoadSceneInto`, both `SaveScene`s and `CopyScene` on the facade, with `SceneLoadResult` and `SaveResult` | 01 |
| 08 | `Instantiate`, `InstanceResult`, `Unpack` and `FindInInstance` | 07 |
| 09 | FenrirScripting's README and Fenrir.Assets' docs, and ScriptingDemo loading a C# data asset and a scene from C# | 06, 08 |

Tickets 01, 02, 03 and 06 touch `packages/FenrirScripting/Managed/` and `packages/FenrirScripting/src/`; 05, 07 and 08 touch `packages/FenrirAssets/Scripting/` (its `AssetsScripting.cpp`, binding configuration and fixture); 04 touches only `packages/Iris/Tools/Iris.Bindings/`. So 01, 02, 03 stack; 07, 08 stack off 01 beside 02 and 03; 04 runs off 02 beside 03; 05 is cut once 03, 04 and 08 are in the base and 06 stacks on it; 09 last.

## Out of scope

Everything the four specs before it own: the registry, kinds and commands (types); the value tree, bridge, upgraders, tree encoding and `ReadTree` (format); `AssetKind::Data`, the envelope, the store doors, references, the move tool, the `AssetSource` seam and hot reload (data-assets); scene files, `EntityId`, `Instance`, the scene doors in C++ and the scene patch (scenes). The project file, `Project::Load` and import sidecars (dev-time). `Edit` from C#; C#-defined scene or app resources (ADR-0029); a C# `Scene` type or a scene-switching door; a C# JSON5 reader; crossing diagnostics or root lists to C#; C# upgraders for C++ types; per-entity behaviour objects (ADR-0007); and the map's fog: the cooked form (which may adopt the tree encoding), game save files, soft references, value inheritance between files and editing support.

## Gaps settled here

- Public read-write auto-properties are described beside public fields, since the generated classes for C++ types are properties and a C# class extending one should read the same way; ADR-0061 names fields only.
- `TreeNode`, `TreeReader` and `TreeWriter` in `Fenrir.Scripting` are the one hand-written managed consumer of the binary tree encoding; ADR-0069's hand-written list does not name them, but every codec needs one.
- A C# type's default is `new T()` run through its codec into a tree, carried in the description entry; the entry itself crosses as a tree in the binary tree encoding through one native, as components report through `Report<T>`.
- Upgraders are not crossed as function pointers: the generated `DataAssetTable` exposes one `Upgrade(name, fromVersion, bytes)` bound by name, as the system host's methods are, and admission records the versions each type covers.
- Codec lookup is `DataAssetCodecs`, keyed by `Type`, filled by the script's table and by a module initialiser in each contract; generated C++-type classes carry `[NativeDataAsset]` so the scripting generator skips them.
- `TryGet<T>` costs one native call per check: `DataAssetTree` takes the known revision and returns bytes only when it moved, so a hit is one crossing with no copy.
- `Add<T>` and `Save` join the facade beside `TryGet<T>` and `Replace<T>`; the result structs carry status and counts but not the roots list or the diagnostics, and `LoadScene` from C# creates a named scene the C++ app activates, since no scene-switching door exists or is added.
- The metadata attributes take the commands' names (`[DisplayName]` for `@display`, `[ReadOnly]` for `@readonly`) and live in `Fenrir.Scripting`, so a script that also imports `System.ComponentModel` qualifies them.
