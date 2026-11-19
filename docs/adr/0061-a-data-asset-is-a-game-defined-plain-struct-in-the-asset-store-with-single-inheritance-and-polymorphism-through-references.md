---
status: accepted
---

# A data asset is a game-defined plain struct in the asset store, with single inheritance and polymorphism through references

Games need typed data in files that is neither a scene nor an imported asset: a weapon's numbers, a tuning table, a dialogue node (Godot's `Resource`, Unity's `ScriptableObject`, Unreal's `DataAsset`). The user wants Godot's workflow, where a field typed as `WeaponData` accepts a `Sword` file; the research found that every engine gets this from the referenced file naming its own type and a registry answering is-a, and that none uses a bag of parts for data. ADR-0060 reads data assets through generated visitors rather than offsets, so a C++ struct whose base has data members is describable.

We decided that **a data asset is a plain C++ struct (or C# class) in the `data-asset` role, owned by the asset store like any asset, that may extend one other data asset type, and that polymorphism is reached only through references**:

1. **Single inheritance, no virtual functions.** A data asset type may extend one other data asset type; the type registry records the base chain, and "assignable to a base" is is-a over it. A field holding `AssetHandle<WeaponData>` accepts a file whose type is `Sword`. A C# data asset type may extend a C++ one through its generated C# class, or another C# type; C++ never extends a C# type.
2. **Polymorphism only through references.** ADR-0060's open slot takes its reference form only: a mixed list of `Effect`s is a list of handles to data assets, embedded in the same file or external. No `std::unique_ptr<Base>` by value, no virtual destructor, no registry-driven destruction inside a struct.
3. **Same store, same handle, one kind.** Data assets are one new `AssetKind::Data` in FenrirAssets' store, keyed by path, deduplicated, owned by the app or a scene and freed with the last owner (ADR-0021); the type is checked through the registry. Game code holds the existing two-field `AssetHandle<T>` with `T` any data asset type; C# holds the same handle.
4. **One data asset per file, with embedded sub-assets.** A file holds one top-level data asset and may embed named sub-assets, referenced from the top-level one or from outside as `file#name`, as a model's labelled sub-assets are. A table is a list field of nested structs; several unrelated top-level assets in one file are not offered.
5. **What it holds:** the described kinds (numbers, bools, enums, strings, lists, optionals, nested structs by value, maps if the format keeps them), handles to assets of any kind, and any described plain struct by value, a component type included. Never an `Entity`: a data asset is not in a scene.
6. **Shared, read-only instances; edits through the store.** `Get` returns a const reference to the one instance per file; `Edit` and `Replace` change it and bump its revision, as material edits do, so hot reload, inspector edits and code edits share one path and every holder sees the change. Data assets may be added from code and are written to disk only when asked.
7. **Defaults from the type.** A field left out of a file takes the type's declared default and the writer omits fields equal to it, so changing a default changes every unoverridden file. A data asset serving as another file's default (Unreal's data-only Blueprint, Stride's archetype) is not offered.
8. **No engine base, no behaviour.** The selection's role (C++) or an attribute (C#) makes a type a data asset; the store, not the struct, holds its path, revision and owner. C++ types are plain data; C# classes may have methods, but only fields are described; logic lives in systems.

## Considered options

- **Composition: a data asset as a bag of typed parts, like an entity.** Fits the ECS and plain data, but no engine does it for data, a field asking for "anything with a `Damage` part" has no file-level precedent, and it gives up the hierarchy the user asked for; nested structs by value and references give composition where it is wanted.
- **A sealed struct with no polymorphism.** Cheapest, but a base-typed field could not accept a derived file, which is the one Godot habit the effort set out to keep.
- **Polymorphism by value inside a struct** (Unity's `SerializeReference`, Unreal's instanced structs). Where every engine pays in pickers, metadata and ownership rules; a reference to an embedded sub-asset reads the same in the file and costs the C++ nothing.
- **A separate data store with its own handle.** Would duplicate ownership, revisions and the C# crossing that the asset store already has.
- **An engine root type every data asset derives from** (Godot's `Resource`). Gives every type a name and version for free, but the description already carries both, and a root would force every C# class onto one base.

## Consequences

- `tools/describe-api` records a base type for the `data-asset` role and accepts data members in bases there only; components keep refusing them. The generated visitor walks the base's fields first.
- FenrirAssets gains `AssetKind::Data`, loading and saving through the type registry, `Get`, `Edit`, `Replace` and `Add` for data assets, and sub-asset labels inside data asset files. The binding generator treats `AssetHandle<T>` for a data asset `T` as the one bound layout it already knows.
- The asset-identity, format, load/save and C# tickets of the serialisation map decide how a reference is spelled, what a wrong-typed reference reports, which package reads the file, and how a C# instance's strings and lists cross.
- The first engine-provided data assets are the asset data types edited only in code today (materials, instance lists, bone masks), once the format is chosen.
- *Narrows ADR-0060:* the open slot is a reference slot; a by-value open slot is not built.
