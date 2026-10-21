# How is rendering modelled in the ECS?

Type: grilling
Status: resolved
Blocked by: 13

## Question

What components and systems does FenrirRenderer give game code, following FenrirPhysics's facade?

- Which components make an entity visible (a mesh and a material on one entity, or several meshes per entity through children)?
- Is the renderer's per-scene state a scene resource, and what happens to it while its scene is inactive?
- `Renderer::Install(App&, RendererConfig)`: what it owns, and how it sits beside `Physics::Install` and FenrirWindow. The ECS facade sits over a renderer core with no ECS dependency (ADR-0019): what crosses between them, beyond the draw list and views.
- What game code may read back (e.g. bounds), and what is invisible or ignored (an entity with a mesh but no material)?
- How the world transform reaches the draw list, read directly in the `Render` phase after `AfterTicks`' interpolation (ADR-0019): `GetWorldMatrix` per item, or something cheaper.

## Answer

ADR: `docs/adr/0023-each-drawn-mesh-is-one-meshrenderer-read-directly-each-frame.md`. Glossary: MeshRenderer, Fallback material.

- **What makes an entity visible:** one `MeshRenderer { AssetHandle<Mesh> mesh; <material> material; bool visible = true; }` per drawn mesh, as the data-oriented ECS engines do (Bevy, Unity's Entities Graphics); a mesh with several materials is several child entities. The material field's type is ticket 17's. `visible` is not inherited; inherited visibility is in Not yet specified.
- **Ignored or invisible:** an empty or stale mesh is skipped (stale logged once per handle); an empty or stale material draws with the fallback material (flat magenta, unlit), logged once; a missing `Transform` is the identity; no `MeshRenderer`, no draw.
- **Per-scene state:** the renderer keeps no scene resource of its own; only the active scene is read; GPU resources are process-wide, keyed by asset handle and revision, freed when a handle goes stale. Scene-wide render settings from tickets 16 and 17 are a scene resource ignored while inactive (ticket 18's `DebugDraw` is one, read in `Render`).
- **`Renderer::Install(App&, RendererConfig)`:** a move-only owner of the core and device that installs the `Render`-phase systems; FenrirAssets and the main window first; destroyed before the App; nothing to do with FenrirPhysics. Crossing to the core each frame: asset data to prepare as `(key, revision, data)`, the draw list, the views, debug and overlay submissions. The core uses FenrirAssets' data types, never its store.
- **Read-back:** `Renderer::GetWorldBounds(Entity)`, computed on call from `MeshData`'s local bounds and the world matrix; `Renderer::Stats()` with last frame's draw, pass and upload counts. Nothing else.
- **World transforms:** `GetWorldMatrix` once per item in the `Render` read; a private per-frame memo of ancestors only if a profile asks.
- **Spawning models:** `SpawnModel(scene, AssetHandle<Model>, parent = {})` in the renderer's facade, one entity per node (`Name`, `Transform`, parent link, a `MeshRenderer` per mesh); animation attaches its parts from the same node tree.
