# What can the engine describe and name today, and what gap do files open?

Type: research
Status: open
Blocked by: none

## Question

Read this repository, not the web, and record exactly what the engine already has that reading and writing files would build on, so the reflection and data asset tickets argue from facts:

- **The API description** (`tools/describe-api`, `SCHEMA.md`, ADR-0042, ADR-0043, ADR-0044, ADR-0045, ADR-0046): which kinds, roles and categories it records; how defaults and doc comments are captured; what it refuses (strings, `std::vector`, inheritance, references); which packages run it and when (committed output, CI's dirty-tree check); whether a game project could run it on its own headers today and what that would take.
- **The C# side** (`packages/Iris`, `FenrirScripting`, the binding generator): what the script report knows about a C# component or shared struct at admission (names, offsets, kinds), how `DynamicComponentType` lays out a script component in the ECS, how native resources and bound types cross, and whether a C# class with strings and lists could be described the same way.
- **The ECS** (`FenrirECS`): `ComponentType` and `DynamicComponent` as the run-time door to a component known only by name; `EntityList`'s create, add, remove, reparent and query; `ComponentWatch` and tracked sets; whether an entity can be named or found by anything but its handle; what `WorldTransform` and parenting need when a hierarchy is rebuilt from a file.
- **Scenes and the app** (`FenrirScene`, `FenrirApp`): what a `Scene` holds (name, entity list, typed resources behind `type_index`), how scenes are created, activated and destroyed, and what a scene resource would need to be written to a file and read back (today it is reached by C++ type only).
- **Assets** (`FenrirAssets`, ADR-0021): how a load names a file (asset root, relative path, `#label` sub-assets), deduplication by path, owners, revisions, `AssetHandle<T>`'s generational form and its bound-type crossing, which asset kinds exist and which are only ever made in code (`MaterialData`, instance lists, bone masks, render-target images), and how a component holding an `AssetHandle<Mesh>` could be written to a file (by what name) and read back (loaded when?).
- **Entity references in components:** every shared component whose field is an `Entity` (joints, `SkinnedMesh`, `BoneAttachment`, animation layers, vehicles' wheels) and how each is resolved today, as the list a scene file must be able to write.
- **Text and names in components:** every fixed-size name field (`BoneName` and others) and how it is bounded, as the precedent for strings.
- **Existing reload:** how the script reload watcher works (`FenrirScripting`), as the precedent for hot reload of files.

Write findings to a Markdown file on the `research/engine-as-is` branch (`.scratch/fenrir-serialisation/research/engine-as-is.md`), citing file paths and the commit read, and link it here.
