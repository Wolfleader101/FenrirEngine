# How is rendering modelled in the ECS?

Type: grilling
Status: open
Blocked by: 13

## Question

What components and systems does FenrirRenderer give game code, following FenrirPhysics's facade?

- Which components make an entity visible (a mesh and a material on one entity, or several meshes per entity through children)?
- Is the renderer's per-scene state a scene resource, and what happens to it while its scene is inactive?
- `Renderer::Install(App&, RendererConfig)`: what it owns, and how it sits beside `Physics::Install` and FenrirWindow. The ECS facade sits over a renderer core with no ECS dependency (ADR-0019): what crosses between them, beyond the draw list and views.
- What game code may read back (e.g. bounds), and what is invisible or ignored (an entity with a mesh but no material)?
- How the world transform reaches the draw list, read directly in the `Render` phase after `AfterTicks`' interpolation (ADR-0019): `GetWorldMatrix` per item, or something cheaper.
