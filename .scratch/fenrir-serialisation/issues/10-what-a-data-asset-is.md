# What is a data asset: its type model, its name, and how C++ and C# define one?

Type: grilling
Status: resolved
Blocked by: 01, 09

## Question

What is the game-defined typed data in a file, and what is it called? **ADR**, and a glossary entry.

- **Type model.** Class inheritance (a type extends another; a field typed as the base accepts any derived file, as Godot's `Resource` does), composition (a data asset is a bag of typed parts like an entity is a bag of components, and a field asks for anything with a given part), a sealed struct with no polymorphism, or a mix. What each gives the user's liked Godot workflow, what each costs the reflection decided in [Where do the type descriptions come from?](09-where-type-descriptions-come-from.md), and how C# expresses it (a base class, an attribute, an interface).
- **Name.** *Data asset*, *definition*, *archetype*, *record*, or another; not *resource* (a scene or app resource) and not *blueprint* (Unreal's). The glossary term, its *avoid* list, and the C++ and C# spellings.
- **Shape.** One instance per file, or several in one file (a table); sub-objects embedded in the file versus their own files; whether a data asset may hold assets (a mesh handle), other data assets, entities (no: it is not in a scene), and fixed-layout components.
- **Ownership and sharing.** Owned by FenrirAssets behind an `AssetHandle<T>` like any asset (one instance per file, shared by every user, freed with its last owner), or a separate store; whether it can be made in code and never saved; whether game code may mutate a loaded instance and what that means for other users and for saving.
- **Defaults and the type's own defaults.** A field left out of the file takes the type's default (as `T{}` does for components); whether a data asset can itself be a default for others (Unreal's archetype).

Consult `codebase-design`. Zoom into [How do engines type, inherit and reference data assets?](01-how-engines-type-data-assets.md).

## Answer

ADR: `docs/adr/0075-a-data-asset-is-a-game-defined-plain-struct-in-the-asset-store-with-single-inheritance-and-polymorphism-through-references.md` (narrowing ADR-0074's open slot to its reference form). Glossary: Data asset; Asset and Sub-asset widened. One grilling round, every recommendation accepted.

- **Type model:** single inheritance, no virtual functions. A data asset type (plain C++ struct, C# class) may extend one other data asset type; the registry records the base chain and "assignable to a base" is is-a over it, so `AssetHandle<WeaponData>` accepts a `Sword` file. Polymorphism only through references: a mixed list is a list of handles to data assets, embedded or external; no by-value open slot, no `unique_ptr`, no virtual destructors. A C# type may extend a C++ data asset type (through its generated C# class) or another C# type; C++ never extends C#.
- **Name:** *data asset*. Avoid resource, scriptable object, data table, blueprint, config, definition, archetype.
- **Store and handle:** one new `AssetKind::Data` in FenrirAssets' store, keyed by path, deduplicated, owned by app or scene, freed with the last owner (ADR-0021), type checked through the registry; game code and C# hold the existing `AssetHandle<T>` with `T` any data asset type. No separate store.
- **Shape:** one top-level data asset per file, with named embedded sub-assets referenced as `file#name`; tables are list fields of nested structs; several unrelated top-level assets per file are not offered.
- **Contents:** the described kinds, handles to assets of any kind, any described plain struct by value (a component type included); never an `Entity`.
- **Ownership and sharing:** `Get` gives a const reference to the one shared instance; `Edit` and `Replace` bump the revision as material edits do, so hot reload, inspector edits and code edits share one path; `Add` from code; written to disk only when asked.
- **Defaults:** a field left out takes the type's default, and the writer omits fields equal to it; a data asset as another file's default ("based on", Unreal's data-only Blueprint, Stride's archetype) is not offered and goes to the fog.
- **No engine base, no behaviour:** the selection role (C++) or an attribute (C#) makes a type a data asset; the store holds path, revision and owner; C++ types are plain data, C# methods are not described, logic lives in systems.
- **Left to other tickets:** how a reference is spelled and what a wrong-typed one reports (11), the file format and omitted-default rules (12), which package reads and writes (15), how C# strings and lists cross (16).
