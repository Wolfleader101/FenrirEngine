# How do engines serialise scenes and prefabs?

Type: research
Status: open
Blocked by: none

## Question

How do engines write a scene (a tree of entities or nodes with their components, and the scene's own settings) to a file, and how do they make part of a scene reusable, in the areas that constrain Fenrir's scene file: Godot's `.tscn`/`PackedScene` (inherited scenes, instanced scenes, editable children), Unity's YAML scenes and prefabs (nested prefabs, prefab variants, overrides, `fileID`/`guid` references), Unreal's levels (actors, Blueprints as prefabs, Level Instances and Packed Level Actors, World Partition's one-file-per-actor), Source's `.vmf` and Source 2's `.vmap` (entities, instances, prefabs, visgroups), Bevy's `DynamicScene` and the BSN scene format work, and an ECS-first format such as Flecs's or EnTT's snapshots.

- **Entity identity in the file:** how an entity is named so references to it survive editing and reload (sequential ids, GUIDs, paths), and how a component's reference to another entity (a joint's two bodies, a skinned mesh's skeleton) is written.
- **Component data:** how a component's fields are written, how a type the engine no longer knows is kept or dropped, and how script components sit beside native ones.
- **Hierarchy and ordering:** how parent-child is encoded and whether sibling order is meaningful.
- **Scene-level state:** environment, sky, physics settings, what Fenrir calls scene resources.
- **Prefabs:** how an instance names its source, what an override is (per-field, per-component, added or removed children), how nesting and variants work, how changes to the source propagate, and how a prefab is "unpacked".
- **Composition and additive loading:** scenes inside scenes, several scenes loaded at once, streaming cells.
- **Merge-friendliness:** what each format does for diffs and version-control merges (stable ordering, one-entity-per-file, smart merge tools).

Write findings to a Markdown file on the `research/engine-scene-files` branch (`.scratch/fenrir-serialisation/research/engine-scene-files.md`), citing each claim's source and pinning the versions read, and link it here.
