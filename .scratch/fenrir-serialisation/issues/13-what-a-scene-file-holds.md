# What does a scene file hold, and how are entities identified and referenced in it?

Type: grilling
Status: open
Blocked by: 02, 09, 11

## Question

What is written when a FenrirScene is saved, and how does what is read back become the same scene?

- **Contents:** the scene's name; its entities with their components (native and C# script components alike, through the descriptions of [Where do the type descriptions come from?](09-where-type-descriptions-come-from.md)); the parent hierarchy and whether sibling order is kept; the scene resources (Sky, SkyLight, physics settings and the rest, today reached by C++ type only); and what is deliberately not written (runtime-only components, transient state, the private state packages keep per entity).
- **Entity identity:** how an entity is named in the file so that a component's `Entity` field (a joint's bodies, a skinned mesh's skeleton, a bone attachment's skeleton, an animation layer's skeleton, a vehicle's wheels) is written and resolved on load; whether an entity has a stable id or name beyond its handle, where it lives (a component, the file only), and how a reference to an entity in another scene or outside the file is refused or allowed.
- **Order of loading:** entities and components are created in which order so that components referring to others resolve, and how systems that react to adds (watches, reconcile steps) see a file load (one structural change, or many).
- **Dynamic and unknown components:** a component whose type the build lacks (a script not yet loaded, a removed type): kept for round-tripping, dropped with a diagnostic, or refused.
- **Scenes in code:** whether the demo scenes' spawn functions and `SpawnModel` stay as they are beside files, and whether a scene can be built in code and saved.

Zoom into [How do engines serialise scenes and prefabs?](02-how-engines-serialise-scenes-and-prefabs.md) and the ECS and entity-reference findings of [What can the engine describe and name today?](08-what-the-engine-can-describe-today.md); asset references follow [How are assets identified?](11-asset-identity-and-references.md).
