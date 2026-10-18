# How is a character body modelled in the ECS?

Type: grilling
Status: open
Blocked by: 30, 31

## Question

How does game code make and drive a character body, given ADR-0011 (plain-data components that physics diffs; anything switched during play is a field edit) and the glossary's **character body** (moved by game code through the physics world with character-style rules, not simulated as a rigid body)? Findings: [What does Jolt offer for characters?](30-what-jolt-offers-for-characters.md) and [How do other engines expose characters?](31-how-other-engines-expose-characters.md).

- **The component:** a `CharacterBody` holding its settings (max slope, step height, ...); whether its shape is its own `CollisionShape` value or the entity's `Collider`.
- **Exclusivity:** a character body and a `RigidBody` on one entity (refused, or one wins); colliders on child entities; whether a character can be a compound.
- **Driving it:** a desired-velocity field that physics applies each tick (Godot's `velocity`), or a native move call that returns a result (Unity's `Move`); what C# systems need, given ADR-0009's limits on native functions.
- **Write-back:** what physics writes into the component or its `Transform` (position, velocity, ground state), and how its own writes are not mistaken for edits.
- **Teleports:** writing the character's `Transform`.
- **Up direction and gravity:** fixed Y-up, or per character.

Probably an ADR.

Facts from "What does Jolt offer for characters?": `CharacterVirtual` (collide-and-slide, recommended for players) is not a body; it is invisible to bodies, queries and triggers unless it has a kinematic inner body. `Character` is a dynamic rigid body locked upright. Some settings are construction-only (padding, predictive distance, iterations), so editing them recreates the character. `Update` never changes velocity; the host owns gravity and ground velocity.

From "How do other engines expose characters?": the options are a velocity field physics applies in its own phase (O3DE, Unreal, Tnua), a one-shot request field cleared after the move (bevy_rapier), or a move call returning a result (Unity, Avian, Rapier); Godot's field-plus-call is labelled a "Hack" off the physics frame, and Unreal's Mover takes inputs only. A ground marker added and removed on landing (Avian's examples) is a structural change on every landing.
