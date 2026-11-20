# How does one entity get its own material parameters?

Type: grilling
Status: resolved
Blocked by: none
Settles: [One entity cannot have its own colour without its own material](15-per-entity-material-colour.md)

## Question

Each caged friend is the Kiki model in its own body colour, and an active power-up changes Kiki's rim colour ([Where do the assets come from?](../../flightless-frenzy/issues/11-where-assets-come-from.md), [What does the game look like?](../../flightless-frenzy/issues/12-the-look.md)). The renderer map lists per-entity material overrides under "Richer materials". What does the engine offer?

- **The shape:** a tint on `MeshRenderer`, a small fixed block of per-entity values a material shader reads (Unreal's custom primitive data), named overrides of any material parameter (Unity's property blocks, Godot's instance uniforms), or more than one.
- **How a material shader reads it,** including a game's own material shader, and how it meets instance lists' per-copy colour.
- **What it costs:** whether entities with overrides still batch and instance automatically (the lighting spec's instancing and the culling spec's indirect draws), and what changes in the draw list.
- **A model with several materials:** whether an override applies to every sub-mesh, or per material slot (Kiki has body and eyes materials, and only the body changes colour).
- **The C# surface.**
- **Where it is planned:** this map, or the renderer map's "Richer materials" line.

Outcome: a spec-ready design.

## Facts (checked 2026-11-19)

- `MeshRenderer` holds `mesh`, `material`, `visible`, `castShadows` (`packages/FenrirRenderer/include/FenrirRenderer/Components.hpp:24-36`).
- An instance list carries per-copy `colours` (`packages/FenrirAssets/include/FenrirAssets/InstanceList*.hpp:23`); shaders read `CurrentDraw().colour` (`packages/FenrirRenderer/Shaders/include/Fenrir/Draw.glsl:134`), and `builtin:lit` and `builtin:unlit` multiply their base colour by it. A per-draw colour slot exists already.

## Answer

Grilled 2026-11-19 in round 3 of [Engine gaps for Flightless Frenzy](../map.md); every recommendation was accepted. **Spec-ready.**

- **A tint:** `MeshRenderer` gains `colour`, multiplied into the per-draw colour shaders already read (`CurrentDraw().colour`, `packages/FenrirRenderer/Shaders/include/Fenrir/Draw.glsl:134`), as an instance list's per-copy colour is.
- **Custom values:** `MeshRenderer` gains four `custom` vec4 values a material shader reads through `CurrentDraw()` the same way, for a game's own parameters (Kiki's power-up rim colour).
- **Batching holds:** both ride in the per-copy data automatic instancing already writes, so entities with different values still instance and cull together.
- **Per material slot for free:** a mesh with several materials is already several entities (ADR-0023), so a friend's body takes its colour and its eyes do not.
- **The C# surface:** the fields on the `MeshRenderer` component.
- **Deferred:** named overrides of any material parameter (Unity's property blocks, Godot's instance uniforms); they come back when a game needs more than four values or a texture per entity.
