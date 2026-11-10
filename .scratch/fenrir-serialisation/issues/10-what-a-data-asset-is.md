# What is a data asset: its type model, its name, and how C++ and C# define one?

Type: grilling
Status: open
Blocked by: 01, 09

## Question

What is the game-defined typed data in a file, and what is it called? **ADR**, and a glossary entry.

- **Type model.** Class inheritance (a type extends another; a field typed as the base accepts any derived file, as Godot's `Resource` does), composition (a data asset is a bag of typed parts like an entity is a bag of components, and a field asks for anything with a given part), a sealed struct with no polymorphism, or a mix. What each gives the user's liked Godot workflow, what each costs the reflection decided in [Where do the type descriptions come from?](09-where-type-descriptions-come-from.md), and how C# expresses it (a base class, an attribute, an interface).
- **Name.** *Data asset*, *definition*, *archetype*, *record*, or another; not *resource* (a scene or app resource) and not *blueprint* (Unreal's). The glossary term, its *avoid* list, and the C++ and C# spellings.
- **Shape.** One instance per file, or several in one file (a table); sub-objects embedded in the file versus their own files; whether a data asset may hold assets (a mesh handle), other data assets, entities (no: it is not in a scene), and fixed-layout components.
- **Ownership and sharing.** Owned by FenrirAssets behind an `AssetHandle<T>` like any asset (one instance per file, shared by every user, freed with its last owner), or a separate store; whether it can be made in code and never saved; whether game code may mutate a loaded instance and what that means for other users and for saving.
- **Defaults and the type's own defaults.** A field left out of the file takes the type's default (as `T{}` does for components); whether a data asset can itself be a default for others (Unreal's archetype).

Consult `codebase-design`. Zoom into [How do engines type, inherit and reference data assets?](01-how-engines-type-data-assets.md).
