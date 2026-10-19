# 05: Teleports, rotation, parents and interpolation

**What to build:** Game code respawns a character by writing its position, turns it every tick by writing its rotation, may put it under a parent, and sees it move smoothly at any frame rate (spec: "the `CharacterBody` component" (pose, parents), "the move in `PostTick`" (interpolation, teleports)).

- **Teleports:** only a world *position* differing from physics's copy, including one caused by a moved parent. No sweep; contacts and `state` refreshed at once from that tick's move; interpolation snapped; carried velocity cleared (once ticket 07 has it); `velocity` kept.
- **Rotation:** the shape takes the entity's full world rotation. A rotation change alone is applied without a sweep, is not a teleport, and blends; the next move pushes the shape out of anything it now overlaps. Up stays world +Y whatever the rotation. The private ground rule is recomputed when rotation changes.
- **Parents:** the character moves in world space; its local `Transform` is written back against its parent's world transform, parents before children.
- **Interpolation:** `CharacterBody::interpolate` follows `RigidBody::interpolate`'s rules, under `PhysicsConfig`'s app-wide switch. Position and rotation blend between the last two steps; stair step-ups and ground snaps blend; a teleport and a new character snap.
- **DemoApp:** T teleports to the start, 2 toggles `interpolate`; the autopilot ends each route with T.

**Blocked by:** 03 (Character tracer bullet).

**Status:** ready-for-agent

- [ ] Tests: a position write teleports with no sweep through a wall between, refreshes `state` that tick, keeps `velocity`, and is left out of `movedVelocity`; a rotation-only write is not a teleport and blends.
- [ ] Tests: a parented character is written back against its parent; moving the parent teleports it.
- [ ] Tests: at frames of 0.3 ticks, position and rotation are the blend of the last two step poses within 1e-4; a teleport and a new character snap; `interpolate = false` and the app-wide switch turn blending off.
- [ ] T snaps the character to the start; 2 toggles interpolation; keys run under `FENRIR_DEMO_KEYS=1` with no errors (checked by running).
