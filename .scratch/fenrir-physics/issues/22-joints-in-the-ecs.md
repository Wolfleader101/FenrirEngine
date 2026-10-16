# How are joints modelled in the ECS?

Type: grilling
Status: open
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
