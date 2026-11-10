# How do engines type, inherit and reference data assets?

Type: research
Status: open
Blocked by: none

## Question

How do engines model game-defined typed data in files (Godot's `Resource`, Unity's `ScriptableObject`, Unreal's `UDataAsset`, `UPrimaryDataAsset` and `UDataTable`, Bevy's typed assets with `Reflect`, Flax's JSON assets, Stride's assets, O3DE's asset system), in the areas that constrain Fenrir's data asset:

- **Typing:** how a file names its type, and whether the type is a class with fields, a plain struct, a table row, or a bag of parts. How script-defined (GDScript, C#, Blueprint, Lua) and native types share the one mechanism.
- **Inheritance and polymorphism:** whether one data asset type extends another; whether a field typed as a base accepts a derived file (Godot's `export var weapon: WeaponData` taking a `Sword.tres`); how Unreal's class-typed soft references and `TSubclassOf`, Unity's polymorphic `SerializeReference`, and composition-based alternatives (parts, traits, interfaces) each do it, and what each costs in tooling.
- **References:** how one data asset names another and how a component names a data asset: by path, by stable id (Unity GUID, Godot UID, Unreal object path), or both; what happens when a file moves; sub-resources embedded in a file versus external files (Godot's `sub_resource`, Unity's sub-assets, Unreal's subobjects).
- **Defaults and inheritance of values:** per-type defaults, Unreal's archetypes and Class Default Objects, Godot's `local_to_scene`, Unity's prefab-like variants for data.
- **Lifecycle:** loading (eager, lazy, async), sharing (one instance per file, duplication), hot reload on edit, and what scripts see when a type changes shape.
- **Versioning:** how a file written by an older type is read (renamed fields, removed fields, custom upgrade hooks, Unity's `FormerlySerializedAs`, Unreal's version custom serialisation).

Write findings to a Markdown file on the `research/engine-data-assets` branch (`.scratch/fenrir-serialisation/research/engine-data-assets.md`), citing each claim's source and pinning the versions read, and link it here.
