# 15: Physics materials and collision layers

**What to build:** Surfaces have friction and restitution, and game code decides what collides with what (spec: "FenrirPhysics: layers and filtering"; glossary: physics material, collision layer). `PhysicsMaterial` is an optional component. `CollisionFilter { layers, mask }` uses 32 Godot-style layers: two bodies collide when each one's mask includes a layer of the other. The filter is read from the body's entity and applies to all its solid colliders (one on a solid child collider is ignored with a warning). `PhysicsConfig` can name layers and give each a default mask. Privately, each distinct `(layers, mask)` in use gets its own Jolt object layer.

**Blocked by:** 12 (Physics tracer bullet).

**Status:** ready-for-agent

- [ ] Tests: a bouncy material bounces higher than a dead one; a low-friction slope slides further.
- [ ] Two bodies whose masks exclude each other pass through; one-sided masks do not collide (the rule is symmetric); the default collides with everything.
- [ ] More than 16 layers in use at once works.
- [ ] Changing `layers` or `mask` at run time takes effect on the next tick.
- [ ] Named layers can be looked up by name; names are fixed at `Install`.
