# How does C# define data asset types and reach scene and asset data?

Type: grilling
Status: resolved
Blocked by: 10, 15

## Question

What does the C# side of data assets and scene files look like, through the existing scripting pipeline (ADR-0042, ADR-0043, ADR-0046)?

- **Defining types:** how a C# class or struct becomes a data asset type (a base class, an attribute, an interface), how its description reaches the registry at admission beside C# components, and how a file names it neutrally beside C++ types.
- **Richer fields in C#:** strings, lists and references in a C# data asset, given that C# components cross as blittable structs today; whether a C# data asset is mirrored into native memory, read through natives, or kept managed with the engine holding only its description.
- **Reaching data:** `Assets.Load<T>` and a typed instance in C#; a C# component holding an `AssetHandle<T>` to a data asset; what happens on script reload when a type's shape changes while files and instances exist.
- **Scenes from C#:** loading a scene file, spawning a prefab, saving, through the facade pattern (ADR-0029, ADR-0043).
- **Generated code:** what the binding generator emits per data asset type, and what is hand-written.

Zoom into [What is a data asset?](10-what-a-data-asset-is.md), [Who loads and saves files?](15-load-save-api-and-ownership.md) and the C# findings of [What can the engine describe and name today?](08-what-the-engine-can-describe-today.md).

## Answer

ADR: `docs/adr/0083-csharp-data-assets-are-classes-materialised-from-the-stores-value-tree-and-scenes-reach-csharp-through-the-assets-facade.md`. Glossary: Tree encoding, Codec (and Value tree widened). One grilling round, every recommendation accepted.

- **Defining types:** a `class` implementing `IDataAsset`, extending another C# data asset class or the generated C# class of a C++ data asset type; a new generator in `Fenrir.Scripting.Generators` emits its description entry (full C# name, `[Version]`, `[Formerly]`, editor metadata attributes, base, fields with kinds), its codecs and its `[Upgrader]` registration; the script report carries data asset types and admission checks them all or nothing.
- **Richer fields in C#:** the store holds the value tree as the truth for a C#-typed asset; `TryGet<T>` pulls through a per-generation cache keyed by handle and revision, one native handing the tree in the compact binary tree encoding, the codec materialising the class; `Replace(handle, instance)` serialises back, swaps the tree and bumps the revision; no `Edit` from C#. C++ data asset types reach C# as generated classes over the same tree (`Iris.Bindings` emits class and codecs), so C# never reads native bytes.
- **Reaching data:** the report gains `entity`, `reference` (with target type) and `fixed-string` (`[FixedString]` on a `fixed byte` buffer) so C# components write to scene files like native ones; a script reload rematerialises from kept trees under the unknown-field rule; hot reload is the revision bump the cache respects.
- **Scenes from C#:** the `Assets` facade's doors named as in C++ (`LoadScene`, `LoadSceneInto`, `Instantiate`, `SaveScene` for scene and subtree, `Unpack`, `CopyScene`), each a `@facade` native, with `SceneLoadResult` and `SaveResult` as described shared structs and `FindInInstance(root, sourceId)` for the instance lookup.
- **Generated code:** classes and codecs for C++ types, entries, codecs and upgrader registration for C# types, natives, declarations and the extension maker; hand-written only `IDataAsset`, the attributes, the generation cache, the generic `TryGet<T>` and `Replace<T>`, and facade conveniences.
- **Tests:** a fixture assembly with a C# data asset type extending a C++ one; load, read, `Replace` seen from C++, generation reload with a changed shape, byte-identical write-back; compile-fail for a string in a component and a struct `IDataAsset`.
- **Left to other tickets:** whether the binary tree encoding becomes the cooked shape of a data asset (20).
