# 09: Characters among bodies: triggers, filters, materials and other characters

**What to build:** Triggers, filters and materials treat a character as a body of its own shape, and characters block and ride each other (spec: "FenrirPhysics: other bodies and the inner body" (character against character, filtering, `PhysicsMaterial`, queries), "contact and trigger events" (triggers)).

- **Triggers:** a character sets off `TriggerEnter`/`TriggerExit` during the step through its inner body, with `otherBody` and `otherCollider` both its entity. The move itself never sees triggers.
- **Queries:** `QueryFilter::ignoreBody` takes a character's entity and skips it. Hits carry no character flag.
- **Filtering:** the entity's `CollisionFilter` is both the inner body's filter and the move's, under the one symmetric rule: a character ignoring layer X walks through X's bodies, and X's bodies, triggers and queries masking it out pass through it.
- **`PhysicsMaterial`** belongs to the inner body only: the friction and restitution other bodies feel resting on or hitting the character. No material changes how a character moves. The spike did not build this.
- **Character against character:** characters block each other only through their inner bodies, found by the broad phase; Jolt's `CharacterVsCharacterCollision` is not used. Neither pushes the other. In entity order a later character sees an earlier one's new position. A move sees another character's inner body moving at that character's last `movedVelocity` (`OnAdjustBodyVelocity`), so standing on a character rides it.
- **DemoApp:** a second character on a C++ loop, which blocks the player and, stopped with 6, can be stood on and ridden (its name shows its own ground state).

**Blocked by:** 07 (Platforms and carried velocity): riding another character uses ground velocity.

**Status:** resolved

Built test-first on branch `claude/character-among-bodies-f1d589`; the DemoApp hand check is the user's. Where the build settled something the ticket left open:

- **Already there from ticket 03:** the inner body is a kinematic body in the moving broad-phase tree, so triggers, queries and `ignoreBody` saw characters before this ticket, and each move was already filtered through the inner body's object layer with the trigger tree left out. This ticket's tests pin that (`Tests/CharacterAmongBodies.cpp`).
- **Live edits:** a character's `PhysicsMaterial` (friction and restitution of the inner body) and `CollisionFilter` (the inner body's object layer, and so its moves') are read from its own entity at creation and diffed in step 2 beside teleports, as bodies' are. A filter edit relayers the inner body through the same path as a body's (`World::SetCharacterFilter`), and the move uses the new layer from that tick. `Same` for materials and filters moved to `Same.hpp` to be shared.
- **Another character's inner body** is reported to a move at that character's last `movedVelocity` through the character listener's `OnAdjustBodyVelocity`, from a map of inner bodies kept by the backend (written only between steps, by the thread that moves characters; never touched by the step's jobs). That covers both the contact velocity the move solves against and `UpdateGroundVelocity`, so riding a character is ticket 07's riding unchanged.
- **Neither pushes the other:** with the inner body moving at `movedVelocity`, Jolt's solver let a character walking into another shove it (2 cm in the test, and the shoved one's next move pushed back, so the two crept along). Another character's inner body may push a character only from beneath (contact normal facing up), which keeps riding; beside or above it is a still wall to the solver, while the velocity trim still measures it as a moving wall.
- **A character standing on another is not in the lower one's way:** the lower one's moves skip the inner body of any character whose ground body is its own (`OnContactValidate`). Without it, a rider is a ceiling at rest and the lower character cannot jump; with it, a jump lifts the rider, as it lifts a box on its head (tested).
- **DemoApp:** the second character is a box 0.9 m high and 1.2 m square, so the player can jump onto its flat top, walking a 6 m square loop at 1.5 m/s south of the paddle's lane, clear of every autopilot leg; 6 stops and starts it. Its name is its own ground state, so the player's reading on it is "on ground: Character 2 (on ground: Ground)". 6 is in the smoke keys; the autopilot does not visit it.

- [x] Tests: a character enters and exits a trigger, named as `otherBody` and `otherCollider`; `ignoreBody` set to the character skips it.
- [x] Tests: a `CollisionFilter` masking a layer lets the character pass through that layer's bodies, and those bodies, triggers and queries pass through it.
- [x] Tests: a box resting on a character feels its `PhysicsMaterial`'s friction.
- [x] Tests: two characters block each other; one standing on a moving other rides it with `groundBody` naming it.
- [ ] The second character blocks the player and, stopped with 6, is stood on and ridden ("on ground: Character") (checked by hand).
