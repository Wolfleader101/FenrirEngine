---
status: accepted
---

# Character bodies are a velocity-driven component, not a move call

A **character body** is a `CharacterBody` component that sits beside the entity's own `Collider`. Game code drives it by writing a `velocity` field, and physics applies that field once per tick in its own phase, with no native move call. Physics writes the velocity back, trimmed by what the character hit. All of this follows from ADR-0011's rules: an entity holds at most one component of each type, and anything switched during play should be a field edit, never a structural change, which a C# native function cannot make (ADR-0009). The survey of Unity, Godot, Unreal, bevy_rapier, Tnua, Avian and O3DE is on branch `research/engine-character-models`. The facts about Jolt are on `research/jolt-characters`.

## The model

```cpp
struct CharacterBody
{
    Math::Vec3 velocity; // world space, m/s; written back by physics
    bool enabled = true;
    // live settings (slope, step, strength, ...) decided by later tickets
};
```

- **One kind of character.** It is collide-and-slide, and other bodies, queries and triggers always see it as a kinematic body of its shape. Hits and events name its entity. Privately it is a Jolt `CharacterVirtual` with an inner kinematic body, which game code never sees (ADR-0010).
- **Its shape is the entity's own `Collider`.** The entity's `PhysicsMaterial` and `CollisionFilter` apply to it as to any body.
  - There are no compound characters. A solid collider on a child entity (with no `RigidBody` of its own) is not part of the character: it gets no body, with one warning. A `Trigger` beneath a character works as it does beneath any body.
  - The `Transform` sits at the collider's centre, as for every collider, and physics converts to Jolt's base-of-shape position privately.
- **Beside `RigidBody`, never with it.** An entity with both has an active `RigidBody` and an inactive character body.
  - A character body is also inactive with no `Collider`, or with a shape kind characters cannot use.
  - Inactive means: the component is kept, there is no backend character, one warning is logged, and the character comes back as soon as it is valid. While inactive, the character does not move, nothing sees it, and nothing is written back.
- **Driven by `velocity`.** Game code writes `velocity` in `Tick`, and physics moves the character by it once in `PostTick`.
  - Afterwards physics writes `velocity` back, with the parts that went into what the character hit removed: landing clears falling speed, and a ceiling clears upward speed. It updates its private copy, so the write is not mistaken for an edit.
  - Ground and platform velocity are never written into `velocity`.
- **Pose.** Physics writes back only the position (through `SetWorldTransform`).
  - The shape takes the entity's full world rotation, like any collider. Turning the character, and keeping it upright, is game code's job.
  - Up is world +Y: slope and step rules are measured along it whatever the shape's rotation.
  - Writing the `Transform` is a teleport: no sweep, contacts and ground state refreshed at once, `velocity` kept.
- **Parents.** A character body moves in world space, like a dynamic body. Its local `Transform` is written back against its parent's world transform, and moving the parent teleports it. It is never part of a parent `RigidBody`'s compound.
- **Switching.** `enabled = false` makes the character inert and invisible without a structural change.
  - Every exposed setting is a live field edit.
  - Jolt's construction-only settings (padding, predictive distance, iterations, tolerances) are private tuning, never exposed.
  - Physics never adds or removes a component on a character, so landing or leaving the ground is never a structural change.

## Considered options

- **A native move call returning a result** (Unity `Move`, Godot `move_and_slide`, Avian `MoveAndSlide`, Rapier `move_shape`). Rejected for these reasons:
  - From C# it would be a native function writing `Transform` in the middle of a system, outside physics's tick order and interleaved with the diff.
  - Characters would move in whatever order systems call, not in a deterministic one.
  - The engines show the cost of unconstrained call sites: Godot's "Hack" for calls off the physics frame, and Unity's one-`Move`-per-frame advice.

  The ECS designs (bevy_rapier, Tnua, the Unity DOTS package, O3DE's on-tick velocity, and Unreal's successor Mover) all apply a field or input in physics's own phase instead. Custom movement still has a way in: general motion-test queries (a sweep to a safe position, and a penetration query), as Avian exposes `cast_move` and `depenetrate`.
- **A one-shot translation request cleared after the move** (bevy_rapier). Rejected: "no request" then means no move and no ground update, which is a known bug there. A velocity of zero already expresses "stand still".
- **Leaving `velocity` untouched** (Jolt, Unity's input). Rejected: falling speed would keep growing while the character stands, and a jump into a ceiling would keep pushing until game code noticed. Writing it back is what Godot's `move_and_slide` does to `velocity`, and what Avian's docs ask for by feeding `projected_velocity` back.
- **`MotionType::Character` on `RigidBody`**, by analogy with ADR-0011's motion types. Rejected: the two kinds share almost no settings (mass, damping and angular velocity mean nothing to a character; slope and step mean nothing to a rigid body). Turning a character into a ragdoll is a different set of entities anyway, not a field edit on one body.
- **A `CollisionShape` inside `CharacterBody`.** Rejected: every ECS engine surveyed takes the character's shape from the entity's own collider component, and a second place to hold a shape would split debug drawing, queries and filtering.
- **Compound characters from child colliders** (Godot). Not now: slope, step and room-to-stand rules are simpler with one shape, and capsules cover nearly every character. Adding compounds later changes only setups that already warn.
- **A dynamic-body character** (Jolt's `Character`, Tnua). Rejected for this component: it has no stairs, floor sticking or platforms, and Jolt recommends `CharacterVirtual` for players. A physically pushed character can still be a dynamic `RigidBody` with game code keeping it upright.
- **An optional inner body** (a character invisible to queries and triggers). Rejected as a mode nobody asked for. O3DE's two bodies per character, filed as a bug, do not apply here, since game code never sees the inner body.
- **A per-character up direction** (Jolt, Godot). Not now: nothing in the character spec needs it, and an `up` field defaulting to +Y can be added later without breaking anything.

## Consequences

- Game code reads a move's results one tick later, in the next `Tick`, like every other physics result.
- `velocity` is both input and output, like a dynamic body's `RigidBody::linearVelocity`. Game code that wants the speed it asked for must keep its own copy. The velocity the character actually moved at is separate, read-back state.
- Physics keeps a private copy of each character's pose, velocity and settings to diff, as it does for bodies.
- A `Collider` on a character body's entity is not a static body, so FenrirPhysics's rule for which body a collider belongs to gains a case.
