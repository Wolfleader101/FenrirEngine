# How does one entity get its own material parameters?

Type: grilling
Status: open
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
