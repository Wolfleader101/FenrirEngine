# How are joints modelled in the ECS?

Type: grilling
Status: resolved
Blocked by: 20, 21

## Question

How does game code put a joint between two bodies, given ADR-0011 (plain-data components, edits found by diffing, anything switched during play is a field edit) and the rule that an entity holds at most one component of each type? Findings: [What does Jolt offer for constraints?](20-what-jolt-offers-for-constraints.md) and [How do other engines model joints?](21-how-other-engines-model-joints.md).

- **Where a joint lives:** on its own entity naming both bodies, or as a component on one body naming the other. How a body with several joints is expressed either way, and whether a joint entity sits in the hierarchy (a child of a body?) and what its `Transform` means.
- **Kinds:** one `Joint` component holding a tagged union of kinds (as `Collider` holds `CollisionShape`), or one component type per kind. Changing kind as a field edit vs a structural change.
- **Naming bodies:** by `Entity`, and whether a joint names the `RigidBody` entity or any collider entity of a compound body.
- **Anchors:** local frames per body, a single world-space frame captured at creation, or a joint entity's own `Transform`; what happens when a body is scaled or reparented.
- **Joining to the world:** how a joint with one free end is written (no entity, or a static body).
- **Enabling:** whether a joint has `bool enabled` for toggling without a structural change.
- **Ownership:** the ECS owns joint configuration and physics diffs it; what physics writes back, if anything.

Probably an ADR (hard to reverse, surprising to Unity or Godot readers, a real trade-off).

## Answer

Recorded as ADR-0013 (`docs/adr/0013-joints-live-on-joint-entities-with-explicit-local-frames.md`). Glossary: **joint** and **joint entity**.

- **Where a joint lives:** a `Joint` component on a **joint entity**, naming `bodyA` and `bodyB`.
  - Any entity may be a joint entity, including one of the bodies it joins: a door may hold its own hinge to the world.
  - A body takes part in any number of joints, each on its own entity: a chain of ten links is ten joint entities, and a door with a hinge and a latch is two.
  - Physics never reads the joint entity's `Transform` for the joint, so the entity may sit anywhere in the hierarchy.
- **Kinds:** one `Joint` holding a `JointKind` tagged union (`HingeJoint`, `SliderJoint`, ...), read through `kind.As<HingeJoint>()`, as `Collider` holds `CollisionShape`. Changing kind is a field edit, and recreates the backend constraint.
- **Naming bodies:** `bodyA` and `bodyB` are body entities only: a `RigidBody` entity, or a standalone static collider's entity. A collider child of a compound body is not accepted.
- **Joining to the world:** an empty `bodyB`. Which side is the world is fixed by the API, never swapped privately.
- **Anchors:** explicit `JointFrame frameA, frameB` (`{ Vec3 position; Quat rotation; }`), each local to its body entity's origin. `frameB` is in world space for the world end.
  - Positions follow the body's world scale; rotation does not.
  - Physics converts to the backend's centre-of-mass frames privately, and reports centre-of-mass changes so anchors don't drift.
  - Physics always builds backend constraints in local space.
- **The rest pose** is the two frames coinciding in world space (hinge angle 0, slider position 0). Limits and targets are measured from it, and recreating never shifts it.
- **Authoring:** a free function `FrameFromWorld(bodyA, bodyB, anchor)` computes both frames from one world pose when called. Physics never captures frames on its own.
- **Enabling:** `bool enabled = true` on `Joint`.
- **Ownership:** the ECS owns the configuration; physics watches `Joint` for adds and removes and diffs the rest (ADR-0011). Nothing is written back by this decision.
- **Shape:**

  ```cpp
  struct Joint
  {
      Entity bodyA;              // a body entity
      Entity bodyB;              // a body entity, or empty: the world
      JointFrame frameA, frameB;
      JointKind kind;
      bool enabled = true;
  };
  ```

  Breaking thresholds and the "connected bodies collide" flag are expected as further shared fields, decided by their own tickets.
