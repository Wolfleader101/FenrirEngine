---
status: accepted
---

# C# data assets are classes materialised from the store's value tree, and scenes reach C# through the Assets facade

A data asset may be defined in C# (ADR-0061) and must hold strings, lists and references, which no C# component can: components are `unmanaged` structs admitted all or nothing from a script report, with a Roslyn source generator emitting their field tables, and the ECS stores them as opaque bytes. C++ is the reader of record and C# never parses a file (ADR-0063); the store keeps one shared instance per data asset with a revision (ADR-0061, ADR-0065); hot reload swaps instances under their handles (ADR-0067); a facade is a hand-written part over generated natives (ADR-0043, ADR-0046). A C# class instance cannot live in native memory, and the engine cannot see a C# type's definition at compile time.

We decided that **a C# data asset type is a class described by a source generator, that its truth is the value tree the store holds, with the managed instance a view pulled on demand, and that C++ data asset types reach C# the same way**:

1. **A class implementing `IDataAsset`.** It may extend another C# data asset class or the generated C# class of a C++ data asset type. A generator in `Fenrir.Scripting.Generators` emits its description entry (full C# name as the canonical name, `[Version]`, `[Formerly]` on type and fields, editor metadata attributes mirroring the doc commands, base, fields with kinds), its two codecs, and the registration of `[Upgrader]` static methods. The script report carries data asset types; admission checks them all or nothing.
2. **The tree is the truth; the instance is a view.** The store's `AssetKind::Data` record for a C#-typed asset holds the type name and the value tree, which the registry's visitor walks for the writer, tools and an inspector. `TryGet<T>(handle)` in C# consults a per-generation cache keyed by handle and revision; on a miss one native hands the tree over the wire in `FenrirSerialisation`'s compact binary tree encoding, in memory only, and the generated codec materialises the class. `Replace(handle, instance)` serialises back and the store swaps the tree and bumps the revision. There is no `Edit` from C#. A script reload rematerialises from kept trees under the unknown-field rule; hot reload is ADR-0067's swap, which the cache respects.
3. **C++ data asset types are generated C# classes over the same tree.** `Iris.Bindings` emits, for every `data-asset` role type in a binding's selection, a class with properties (strings as `string`, lists as `List<T>`, references as `AssetHandle<T>`, nested structs as their mirrors) and the same codecs. C# never reads a C++ data asset's bytes; the store converts tree to struct for C++ and tree to class for C#.
4. **Three kinds join the script report.** `entity` for `Entity` fields, `reference` with its target type for `AssetHandle<T>` fields, and `fixed-string` for a `fixed byte` buffer marked `[FixedString]`, so C# components write to scene files as native ones do (ADR-0064). Strings and lists stay out of components in both languages.
5. **Scenes from C# are the `Assets` facade's doors, named as in C++.** `LoadScene`, `LoadSceneInto`, `Instantiate`, `SaveScene` (scene and subtree), `Unpack` and `CopyScene`, each a `@facade`-named native; `SceneLoadResult` and `SaveResult` are described shared structs with status and counts, diagnostics logged on the C++ side; `FindInInstance(root, sourceId)` crosses the instance lookup.
6. **Generated and hand-written.** Generated: the classes and codecs for C++ types, the entries, codecs and upgrader registration for C# types, the natives, declarations and extension maker. Hand-written: `IDataAsset` and the attributes in the scripting contract, the generation cache, the generic `TryGet<T>` and `Replace<T>` over the tree natives, and facade conveniences. No per-type hand-written mapping.
7. **Tests follow the scripting fixtures.** A fixture assembly defines a C# data asset type, one extending a C++ one; tests load, read from C#, `Replace` and see the revision from C++, reload the generation with a changed shape, and write back byte-identical; the compile-fail project refuses a string in a component and a struct `IDataAsset`.

## Considered options

- **Mirroring a C# data asset into native memory as a described struct.** C++ cannot see the type at compile time, and strings and lists have no fixed layout to mirror; the tree is the layout-free form the registry already walks.
- **C# parsing files itself with a managed JSON5 reader.** Two readers of record, two upgrader paths, and a cooked form the C# side would have to read too; ADR-0063 chose one.
- **Per-field natives into a native-side dynamic record** (every property a call). Correct but slow for a tuning table read each frame; one pull per revision and a managed view costs one crossing per change.
- **`Edit` from C# mutating the managed view in place.** Would leave the store's tree stale until a flush nobody remembers to call; `Replace` is one path for both languages.
- **A text encoding over the wire.** The engine would parse JSON5 twice per pull; the binary tree encoding is the in-memory form and a stepping stone the cooked form can reuse.

## Consequences

- `Fenrir.Scripting` gains `IDataAsset`, the data asset attributes and the generation cache; `Fenrir.Scripting.Generators` gains the data asset generator; `Iris.Bindings` emits data asset classes and codecs; the script report and admission gain data asset types and three kinds; `FenrirSerialisation` gains a compact binary tree encoding; `FenrirAssets`' natives gain the tree pull and push and the scene doors.
- A C# holder reading a data asset each frame pays one cache check; a C++ holder of a C#-typed asset sees only the tree, through the visitor.
- The cooked-form ticket may adopt the binary tree encoding as the cooked shape of a data asset.
- *Amended 2026-11-19 by the C# spec:* a C# data asset class's described fields are its public instance fields and its public read-write auto-properties, since the generated classes for C++ types are properties and a class extending one reads the same way; ADR-0061's "only fields are described" is read so. Upgraders cross by name through one generated `Upgrade(name, fromVersion, bytes)` rather than as function pointers.
