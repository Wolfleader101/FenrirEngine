---
status: accepted
---

# A prefab is any scene or model file instanced by an Instance component, with overrides written as a diff against the source

Part of a scene must be reusable: placed many times, edited in one place, adjusted per placement. The research found three shapes: diff against the source (Godot's instanced and inherited scenes, Unity's `PrefabInstance` with per-property modifications and 2022.2's removal lists, Unreal's archetype deltas), copy and fix up (Source 1's `func_instance`, collapsed at compile), and live inheritance (Flecs's `IsA`). Every engine with overrides keeps them per property, Godot forbids removing a source node and users complain, and a stable per-entity id that survives instancing is what every scheme leans on. Fenrir already spawns a model's node tree into a scene through `SpawnNodes`, making a synthetic root named after the file when the file has several roots, and ADR-0078 left open whether a spawned model saves expanded or as one reference. The user likes Godot's inheritance.

We decided that **any scene file or model file can be instanced, that the link is a described `Fenrir::Instance` component on the instance root, and that a host file writes an instance as a reference plus a diff**:

1. **No prefab asset kind.** Any scene file can be instanced into another; nothing in the file declares it a prefab, and *prefab* is the field's informal word. The subtree made from a file inside another scene is a **scene instance**.
2. **The link is a component.** `Fenrir::Instance { source }` (a `fixed-string` asset path) sits on the instance root: the file's single root entity, or a synthetic root named after the file at the identity transform when the file has several, `SpawnNodes`' rule. Inner entities keep their source file's `EntityId`s, which repeat per instance and are scoped by the root.
3. **One mechanism for scenes and models.** The source path's kind decides the expansion: a `.scene.json5` through the scene loader, a model file through `SpawnNodes`. A spawned model therefore saves as one block with overrides, not as its expanded entities.
4. **An instance block is a reference plus a diff.** In the host's `entities`, the block carries `id`, `parent`, `instance: "<path>"`, `components` for the root (merged on top of the source root's; the root `Transform` is the natural override), `overrides: { "<source id>": { components, removed: [type names] } }` for inner entities, `removedEntities: [source ids]`, and added entities as ordinary blocks whose `parent` names an inner entity. Values override per field, components and entities add and remove. The writer computes the diff by loading the source fresh and comparing field by field, so a value equal to the source's is never written and a source edit reaches every instance that did not override it.
5. **Inner entities are addressed by a slash path of ids.** `"<instance id>/<source id>"`, chaining through nested instances. A host `Entity` field may point into an instance by that path; an override on an inner entity may point out at a host entity; the source file never names anything outside itself. A file that instances itself, directly or through a chain, is refused with a diagnostic at the block.
6. **Propagation is at load; nesting and variants are free.** The host holds only the diff, so a source change reaches instances when the host next loads. A source may itself contain instances. A variant, and with it scene inheritance, is a scene file whose single root block is an instance of another scene file, with its own `resources`: no `inherits` key.
7. **`Instantiate` spawns; `LoadSceneInto` merges.** `Assets::Instantiate(Scene&, path, parent = {}) -> InstanceResult` loads the source, marks the root and returns the root, the `SceneLoadResult` and a lookup from source id to `Entity`. The host scene owns what it loaded. `LoadSceneInto` (ADR-0079) stays the unlinked additive merge. `SpawnModel` keeps its signature over the same path.
8. **Unpacking and making a prefab are engine functions.** `Assets::Unpack(Scene&, Entity root)` removes the root's `Instance` so the subtree saves verbatim, one level at a time. `Assets::SaveScene(const Scene&, Entity root, path)` writes a subtree as its own scene file: "make a prefab from this selection", and what a tool calls to apply an edited instance back to its source. The editor gesture "apply these overrides to the source" is editor policy over that save, left to the editing-support fog.

## Considered options

- **A distinct prefab asset kind** (Unity's prefab, Unreal's Blueprint). A second file kind and a conversion step for no capability a scene file lacks; Godot shows one kind suffices.
- **Copy at load with no link** (Source 1's Hammer prefabs). Cheapest, but the host file would hold every expanded entity, a source edit would reach nothing, and a model would save as fifty entities.
- **Live inheritance at run time** (Flecs). Shared storage and instant propagation, but it asks the ECS to resolve a component read through a chain, which Fenrir's plain pages and C# opaque pages do not do; load-time propagation with hot reload covers the workflow.
- **Godot's no-removal rule.** Simpler diff, but removing a child from one placement is the first thing a designer tries; Unity added removals for that reason.
- **A separate `inherits` for scene inheritance.** A second concept for what a root instance block already expresses.
- **The link in the file only, with no run-time component.** The writer, hot reload and an editor would all need to know which entities belong to which instance; the component is that knowledge.

## Consequences

- FenrirECS gains the `Instance` component; `FenrirAssets` gains `Instantiate`, `Unpack`, the subtree `SaveScene` overload, the instance block's reading and diffing, and the slash-path id spelling in the scene loader and writer.
- `SpawnNodes` marks its root with `Instance`; a saved scene that spawned models re-spawns them from their files on load, with any overrides reapplied.
- A source edit reaches running instances only through a reload; the hot-reload ticket decides whether an instance re-expands in place.
- The level-editor ticket builds on instances for kits, in-place editing and per-instance overrides; what it needs beyond this is its own.
- The cooked form may pre-expand instances or keep the references; the cooked-form ticket decides.
