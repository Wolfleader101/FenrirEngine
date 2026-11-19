# What does a scene file hold, and how are entities identified and referenced in it?

Type: grilling
Status: resolved
Blocked by: 02, 09, 11

## Question

What is written when a FenrirScene is saved, and how does what is read back become the same scene?

- **Contents:** the scene's name; its entities with their components (native and C# script components alike, through the descriptions of [Where do the type descriptions come from?](09-where-type-descriptions-come-from.md)); the parent hierarchy and whether sibling order is kept; the scene resources (Sky, SkyLight, physics settings and the rest, today reached by C++ type only); and what is deliberately not written (runtime-only components, transient state, the private state packages keep per entity).
- **Entity identity:** how an entity is named in the file so that a component's `Entity` field (a joint's bodies, a skinned mesh's skeleton, a bone attachment's skeleton, an animation layer's skeleton, a vehicle's wheels) is written and resolved on load; whether an entity has a stable id or name beyond its handle, where it lives (a component, the file only), and how a reference to an entity in another scene or outside the file is refused or allowed.
- **Order of loading:** entities and components are created in which order so that components referring to others resolve, and how systems that react to adds (watches, reconcile steps) see a file load (one structural change, or many).
- **Dynamic and unknown components:** a component whose type the build lacks (a script not yet loaded, a removed type): kept for round-tripping, dropped with a diagnostic, or refused.
- **Scenes in code:** whether the demo scenes' spawn functions and `SpawnModel` stay as they are beside files, and whether a scene can be built in code and saved.

Zoom into [How do engines serialise scenes and prefabs?](02-how-engines-serialise-scenes-and-prefabs.md) and the ECS and entity-reference findings of [What can the engine describe and name today?](08-what-the-engine-can-describe-today.md); asset references follow [How are assets identified?](11-asset-identity-and-references.md).

## Answer

ADR: `docs/adr/0064-a-scene-file-is-authoring-data-holding-described-components-by-entity-id-and-described-scene-resources-and-is-read-once.md`. Glossary: Scene file, Entity id, Transient. Two grilling rounds, every recommendation accepted; the second round came from the user's framing that a scene file is authoring data a game loads once, with save games the game's own.

- **Contents:** one scene per file, no name inside it (the caller names the scene, defaulting to the asset path); a `Fenrir::Scene` envelope whose `data` holds `resources` then `entities`. Every described `component`-role type the entity has is written, native or C#, by canonical name with defaults omitted; undescribed components (state structs, package-private state), `Relationship`, `WorldTransform` and described components marked `@transient` are not; an entity tagged `Fenrir::Transient` is skipped with its children; the save result counts skips. `char[N]` fields become a `fixed-string` kind written as a string.
- **Entity identity:** a described `Fenrir::EntityId` component holding a readable random id, written first, added at load, minted at save when missing. `Entity` becomes an `entity` kind in the API description, written as the target's id, `null` when empty or outside the file (one diagnostic at save), filled from the id map after every entity exists. Hierarchy is `parent: "<id>"` plus file order, depth-first, children after their parent, sibling order rebuilt through `AddChild`.
- **Scene resources:** a map by canonical type name of described `scene-resource` types (`Fenrir::Sky`, `Fenrir::SkyLight`), written before entities and applied after them; behaviour resources are created by their packages and never written.
- **Order of loading:** two passes inside one frame: reserve every entity and build the id map, then add components in file order, resolve entity fields and complete; watches see one add per component, no transaction or event.
- **Unknown components:** kept opaque in a per-entity `Fenrir::UnknownComponents` holder, one diagnostic per type per load, written back unchanged, never attached retroactively.
- **Scenes in code:** spawn functions and `SpawnModel` stay; a code-built scene saves with ids minted then; a spawned model saves expanded as ordinary entities.
- **Role:** authoring data, read when a scene is created from it, never written by the running game; the save path serves tools, an editor and code-built scenes. Save games are the game's own, composed from the public blocks (type registry and visitors, JSON5 reader and writer, entity id map). Scene is the word for a map, as in Godot and Unity.
- **Left to other tickets:** the load and save API including additive loading (15); whether a model instance or sub-scene is kept as one reference (14); whether a changed file re-reads (17); the cooked shape of a scene (20).
