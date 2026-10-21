---
status: accepted
---

# Each drawn mesh is one MeshRenderer, read directly each frame

Game code makes something visible with FenrirRenderer's ECS facade, over the renderer core ADR-0019 describes. The data-oriented ECS engines (Bevy, Unity's Entities Graphics) settle on one draw per entity and split a mesh with several materials into several entities. The object-centred ones (Godot, Unity's GameObjects, Unreal) put a list of material slots on one object instead. FenrirEngine's components are plain data that C# sees as blittable structs, and each draw-list item is one draw, so FenrirRenderer follows the ECS engines.

## The decision

- **One component per drawn mesh.** `MeshRenderer { AssetHandle<Mesh> mesh; <material> material; bool visible = true; }`: one mesh, one material, one draw. A mesh with several materials is several child entities, each with its own `MeshRenderer`. The material field's type is the materials decision's. `visible` hides only its own entity; children are not hidden with it.
- **What is not drawn.** An empty or stale mesh handle is skipped, and a stale one is logged once per handle (ADR-0021). An empty or stale material draws with the **fallback material**, a built-in flat magenta unlit material, logged once, so the mistake shows on screen. A missing `Transform` counts as the identity. An entity with no `MeshRenderer` is never drawn.
- **Read directly, every frame.** In the `Render` phase the facade queries the active scene's `MeshRenderer`s and calls `GetWorldMatrix` once per item for the draw list. Inactive scenes are not read. Nothing is kept between frames, so there is no change tracking.
- **No scene resource of the renderer's own.** GPU resources are process-wide, keyed by asset handle and revision, and freed when a handle goes stale (ADR-0021). Scene-wide render settings that later decisions add are a scene resource, ignored while their scene is inactive.
- **`Renderer::Install(App&, RendererConfig)`** returns the move-only `Renderer` that owns the renderer core and the graphics device, and installs the `Render`-phase systems, as `Physics::Install` does. FenrirAssets and the main window are installed first. Destroy it before the App. It does not interact with FenrirPhysics: entities physics poses (bodies, wheel entities) are drawn because they carry a `MeshRenderer`.
- **What crosses to the core.** The core uses FenrirAssets' data types but never its store. The facade gives it each frame the asset data to prepare as `(key, revision, data)` for whatever is new or changed, the draw list, the views, and the debug and overlay submissions.
- **Read-back.** `Renderer::GetWorldBounds(Entity)` gives an axis-aligned box computed on call from the mesh's local bounds (on `MeshData`) and the world matrix. `Renderer::Stats()` gives last frame's draw, pass and upload counts. Nothing else is read back.
- **Spawning models.** `SpawnModel(scene, AssetHandle<Model>, parent = {})` belongs to the renderer's facade: one entity per node with `Name`, `Transform` and its parent link, and a `MeshRenderer` per mesh (a node with several meshes gets a child entity per mesh). It returns the root. The animation package attaches skeletons and clips from the same node tree.

## Considered options

- **Separate mesh and material components** (Bevy's `Mesh3d` and `MeshMaterial3d`). Rejected: Bevy splits them because its material type is generic and it tracks changes per component. FenrirRenderer has neither, and one component is one query and one C# struct.
- **Material slots on one entity** (Godot, Unity's `MeshRenderer.materials`, Unreal). Rejected: a variable-length list is not plain blittable data, and each draw-list item would need an inner loop.
- **Inherited visibility.** Deferred: useful for spawned models, but not needed by the foundation.
- **A cached world matrix per entity, or a per-frame memo of ancestors.** Deferred: `GetWorldMatrix` is exact, always current after `AfterTicks`, and hierarchies are shallow. A memo inside the facade would be private and invisible behind the draw list.

## When to revisit

If the `Render` read shows in a profile (memo ancestors, or culling and instancing at scale), or if spawned hierarchies want hiding as one.
