# How do prefabs work: instancing a scene file into another, overrides, nesting and inheritance?

Type: grilling
Status: open
Blocked by: 13

## Question

How is part of a scene made reusable, and what is the engine's word for it?

- **Model:** is a prefab simply a scene file instanced into another (Godot's one `PackedScene` for both), a distinct asset kind (Unity's prefab, Unreal's Blueprint or Level Instance), or an entity subtree copied at load with no link back? Does the engine keep the link at runtime (so an instance knows its source) or only the editor and the file?
- **Overrides:** per-field, per-component, added or removed components, added or removed children; how an override is written beside the instance; how a change to the source propagates to instances that did not override it; variants (a prefab of a prefab); and unpacking.
- **Identity across instancing:** how entities inside an instance are addressed by overrides and by references from outside, when the source's own ids repeat per instance.
- **Inheritance of scenes:** whether a scene file can extend another (Godot's inherited scenes), which the user likes in data assets too.
- **Runtime use:** spawning an instance from code and from C# (today `SpawnModel` spawns a model's node tree), with which owner, and how the spawned entities are found by the caller.

Zoom into the prefab findings of [How do engines serialise scenes and prefabs?](02-how-engines-serialise-scenes-and-prefabs.md); the scene file's shape is [What does a scene file hold?](13-what-a-scene-file-holds.md)'s.
