# 09: Characters among bodies: triggers, filters, materials and other characters

**What to build:** Triggers, filters and materials treat a character as a body of its own shape, and characters block and ride each other (spec: "FenrirPhysics: other bodies and the inner body" (character against character, filtering, `PhysicsMaterial`, queries), "contact and trigger events" (triggers)).

- **Triggers:** a character sets off `TriggerEnter`/`TriggerExit` during the step through its inner body, with `otherBody` and `otherCollider` both its entity. The move itself never sees triggers.
- **Queries:** `QueryFilter::ignoreBody` takes a character's entity and skips it. Hits carry no character flag.
- **Filtering:** the entity's `CollisionFilter` is both the inner body's filter and the move's, under the one symmetric rule: a character ignoring layer X walks through X's bodies, and X's bodies, triggers and queries masking it out pass through it.
- **`PhysicsMaterial`** belongs to the inner body only: the friction and restitution other bodies feel resting on or hitting the character. No material changes how a character moves. The spike did not build this.
- **Character against character:** characters block each other only through their inner bodies, found by the broad phase; Jolt's `CharacterVsCharacterCollision` is not used. Neither pushes the other. In entity order a later character sees an earlier one's new position. A move sees another character's inner body moving at that character's last `movedVelocity` (`OnAdjustBodyVelocity`), so standing on a character rides it.
- **DemoApp:** a second character on a C++ loop, which blocks the player and, stopped with 6, can be stood on and ridden (its name shows its own ground state).

**Blocked by:** 07 (Platforms and carried velocity): riding another character uses ground velocity.

**Status:** ready-for-agent

- [ ] Tests: a character enters and exits a trigger, named as `otherBody` and `otherCollider`; `ignoreBody` set to the character skips it.
- [ ] Tests: a `CollisionFilter` masking a layer lets the character pass through that layer's bodies, and those bodies, triggers and queries pass through it.
- [ ] Tests: a box resting on a character feels its `PhysicsMaterial`'s friction.
- [ ] Tests: two characters block each other; one standing on a moving other rides it with `groundBody` naming it.
- [ ] The second character blocks the player and, stopped with 6, is stood on and ridden ("on ground: Character") (checked by hand).
