# 14: Compound bodies and bodies in a hierarchy

**What to build:** Bodies behave correctly in the entity hierarchy (spec: "FenrirPhysics: components", "stepping"; glossary: compound body). A collider on an entity without its own `RigidBody` joins its nearest ancestor that has one, forming a compound body. A dynamic body under a parent simulates independently in world space and writes back `local = parentWorld⁻¹ × world`; kinematic and static bodies follow their world transform when a parent moves.

**Blocked by:** 12 (Physics tracer bullet).

**Status:** ready-for-agent

- [ ] Tests: a body with colliders on two child entities collides as one compound body; adding, removing or moving a child collider rebuilds the compound.
- [ ] A dynamic body under a moved, rotated parent falls and rests at the right world position, and its local `Transform` is consistent with the parent (no spurious teleports step to step).
- [ ] Moving the parent of a kinematic or static body moves the body.
- [ ] Removing a `RigidBody` makes its colliders static; a body left with no collider stays in the ECS, inactive in physics, with a warning.
