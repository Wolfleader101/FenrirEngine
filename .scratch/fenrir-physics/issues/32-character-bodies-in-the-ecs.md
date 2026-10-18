# How is a character body modelled in the ECS?

Type: grilling
Status: resolved
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

## Answer

Recorded as ADR-0014 (`docs/adr/0014-character-bodies-are-velocity-driven-components.md`). Glossary: **character body** sharpened, **collider** amended (a collider on a character body's entity is not static).

- **One kind of character:** collide-and-slide, and always visible to bodies, queries and triggers as a kinematic body of its shape. Hits and events name its entity. Privately it is a Jolt `CharacterVirtual` with an inner kinematic body that game code never sees. No dynamic-body character (a dynamic `RigidBody` covers that), and no invisible mode.
- **The component:** `CharacterBody`, separate from `RigidBody`. If an entity has both, the `RigidBody` works and the character body is inactive.
- **Shape:** the entity's own `Collider`, as in every ECS engine surveyed. The entity's `PhysicsMaterial` and `CollisionFilter` apply as for any body.
  - No compound characters: a solid collider on a child entity with no `RigidBody` of its own is not part of the character, gets no body, and logs one warning. Child `Trigger`s work as beneath any body.
  - Allowed shape kinds are ticket 36's call (Jolt cannot use a triangle mesh).
- **Inactive** (component kept, no backend character, one warning, back once valid) with a `RigidBody` on the entity, with no `Collider`, or with a shape kind characters cannot use. An inactive character does not move, nothing sees it, and nothing is written back.
- **Driving it:** a `velocity` field (world space, m/s). Game code writes it in `Tick`, and physics moves the character by it once per tick in `PostTick` (ticket 33 places it in the sequence).
  - There is no native move call. Custom movement uses ticket 36's motion-test queries, in the style of Avian's `cast_move` and `depenetrate`.
  - C# needs no native function to move a character.
- **Velocity write-back:** after the move, physics writes `velocity` back with the parts that went into what the character hit removed (Godot's `move_and_slide`, Avian's `projected_velocity`). It updates its private copy, so the write is not taken for an edit.
  - Ground and platform velocity are never written into it (ticket 33).
  - The velocity actually moved at is read-back state (ticket 34).
- **Pose write-back:** position only, through `SetWorldTransform`.
  - The shape takes the entity's full world rotation, like any collider (bevy_rapier, Avian, Unity DOTS). Turning the character and keeping it upright are game code's job.
  - The `Transform` sits at the collider's centre, and physics converts to Jolt's base-of-shape position privately.
- **Teleports:** a world pose that differs from physics's private copy is a teleport: no sweep, contacts and ground state refreshed at once, `velocity` kept. Interpolation is ticket 33's.
- **Parents:** the character moves in world space, like a dynamic body. Its local `Transform` is written back against its parent's world transform, and moving the parent teleports it. It is never part of a parent `RigidBody`'s compound. Platforms are ridden through ground velocity, not parenting.
- **Up:** fixed at world +Y. Slope and step rules are measured along it whatever the shape's rotation, and the world's gravity direction does not turn the character. A per-character `up` field can be added later without breaking anything.
- **Settings:** every exposed setting is a live field edit for the next tick. Jolt's construction-only settings (padding, predictive distance, iterations, tolerances, back-face mode) are private tuning, never exposed, so nothing ever needs the character recreated. The field list comes from tickets 33 (slope, step, floor) and 35 (strength, mass).
- **Switching off:** `bool enabled = true`.
  - Disabled: not moved, invisible to bodies, queries and triggers, `velocity` not written back, `Transform` owned by game code.
  - Re-enabling refreshes contacts and ground state as a teleport does.
- **No structural changes by physics:** physics only writes fields on components game code added. It never adds or removes one, so landing, leaving the ground or being blocked is never a structural change. If ticket 34 puts state in its own component (the DOTS split of settings and state), game code adds it, as it adds `ContactEvents`.
- **Shape so far:**

  ```cpp
  struct CharacterBody
  {
      Math::Vec3 velocity = Math::Vec3(0.0f); // world space, m/s; written back by physics
      bool enabled = true;
      // live settings from tickets 33 and 35; read-back state from ticket 34
  };
  ```
