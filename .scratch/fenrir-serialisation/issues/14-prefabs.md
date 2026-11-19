# How do prefabs work: instancing a scene file into another, overrides, nesting and inheritance?

Type: grilling
Status: resolved
Blocked by: 13

## Question

How is part of a scene made reusable, and what is the engine's word for it?

- **Model:** is a prefab simply a scene file instanced into another (Godot's one `PackedScene` for both), a distinct asset kind (Unity's prefab, Unreal's Blueprint or Level Instance), or an entity subtree copied at load with no link back? Does the engine keep the link at runtime (so an instance knows its source) or only the editor and the file?
- **Overrides:** per-field, per-component, added or removed components, added or removed children; how an override is written beside the instance; how a change to the source propagates to instances that did not override it; variants (a prefab of a prefab); and unpacking.
- **Identity across instancing:** how entities inside an instance are addressed by overrides and by references from outside, when the source's own ids repeat per instance.
- **Inheritance of scenes:** whether a scene file can extend another (Godot's inherited scenes), which the user likes in data assets too.
- **Runtime use:** spawning an instance from code and from C# (today `SpawnModel` spawns a model's node tree), with which owner, and how the spawned entities are found by the caller.

Zoom into the prefab findings of [How do engines serialise scenes and prefabs?](02-how-engines-serialise-scenes-and-prefabs.md); the scene file's shape is [What does a scene file hold?](13-what-a-scene-file-holds.md)'s.

## Answer

ADR: `docs/adr/0080-a-prefab-is-any-scene-or-model-file-instanced-by-an-instance-component-with-overrides-written-as-a-diff-against-the-source.md`. Glossary: Scene instance, Override, Variant. One grilling round, every recommendation accepted; the shape is diff-against-source (Godot, Unity, Unreal), with Unity's removals and Godot's one-file-kind.

- **Model and word:** no prefab asset kind; any scene file or model file can be instanced, and the result is a *scene instance*; *prefab* is the field's informal word. The link is a described `Fenrir::Instance { source }` component on the instance root (the file's single root, or a synthetic root named after the file when it has several, `SpawnNodes`' rule), so the running scene, the writer, hot reload and an editor all know which entities belong to which instance. The source path's kind picks the expansion (scene loader or `SpawnNodes`), so a spawned model saves as one reference block, closing what ticket 13 left open.
- **Overrides:** the host block carries `instance`, root `components` merged on top of the source's, `overrides` by source id (fields and `removed` component types), `removedEntities`, and added entities as ordinary blocks parented into the instance. Per field for values, per component and entity for add and remove. The writer diffs the live instance against a fresh load of the source; propagation is at load because the host holds only the diff.
- **Identity across instancing:** inner entities keep their source ids, scoped by the instance root, and are addressed as `"<instance id>/<source id>"`, chaining through nested instances; host fields may point in, overrides may point out, the source never names outside itself; self-instancing is refused with a diagnostic.
- **Inheritance of scenes:** a *variant* is a scene file whose single root block is an instance of another scene file, with its own resources; no `inherits` key. Nesting is free.
- **Runtime use:** `Assets::Instantiate(Scene&, path, parent = {}) -> InstanceResult` (root, `SceneLoadResult`, lookup from source id to `Entity`), owned by the host scene; `LoadSceneInto` stays the unlinked merge; `SpawnModel` keeps its signature. `Assets::Unpack(Scene&, root)` removes the link one level at a time; `Assets::SaveScene(const Scene&, Entity root, path)` writes a subtree as a scene file ("make a prefab", and the save a tool uses to apply an instance back). The editor gesture "apply overrides to source" goes to the editing-support fog.
- **Left to other tickets:** whether an instance re-expands in place on a source change (17); the C# facade for `Instantiate` (16); what the level editor needs beyond instances (19); whether a cooker pre-expands instances (20).
