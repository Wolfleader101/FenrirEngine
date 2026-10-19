# 05: Teleports, rotation, parents and interpolation

**What to build:** Game code respawns a character by writing its position, turns it every tick by writing its rotation, may put it under a parent, and sees it move smoothly at any frame rate (spec: "the `CharacterBody` component" (pose, parents), "the move in `PostTick`" (interpolation, teleports)).

- **Teleports:** only a world *position* differing from physics's copy, including one caused by a moved parent. No sweep; contacts and `state` refreshed at once from that tick's move; interpolation snapped; carried velocity cleared (once ticket 07 has it); `velocity` kept.
- **Rotation:** the shape takes the entity's full world rotation. A rotation change alone is applied without a sweep, is not a teleport, and blends; the next move pushes the shape out of anything it now overlaps. Up stays world +Y whatever the rotation. The private ground rule is recomputed when rotation changes.
- **Parents:** the character moves in world space; its local `Transform` is written back against its parent's world transform, parents before children.
- **Interpolation:** `CharacterBody::interpolate` follows `RigidBody::interpolate`'s rules, under `PhysicsConfig`'s app-wide switch. Position and rotation blend between the last two steps; stair step-ups and ground snaps blend; a teleport and a new character snap.
- **DemoApp:** T teleports to the start, 2 toggles `interpolate`; the autopilot ends each route with T.

**Blocked by:** 03 (Character tracer bullet).

**Status:** resolved

Built test-first on branch `claude/character-teleports-interpolation-16088c`. Where the build settled something the ticket left open:

- A teleport puts the backend character at its new pose and refreshes its contacts at once, so that tick's move starts from where it now is: the test teleports to 0.3 m above the floor, within `floorSnap`, and the character is still in the air after the move, not stuck down to the floor by contacts from where it was.
- A turn sets the backend character's rotation and recomputes the ground rule (Jolt's supporting volume, in the character's own space) from the turned shape. Laid on its side, a capsule rests on its side on the ground; stood up again, the next moves push it out of the floor.
- Physics writes back only the character's position: its local rotation and scale are kept bit for bit, not recomputed through its parent.
- Bodies and characters share one interpolation (`Shown` in `WorldImpl.hpp`), so a character blends parents before children together with the bodies around it. In `BeforeTicks`, the parts of a `Transform` that game code changed during Update are kept, and the rest go back to the true pose. So a character turned in Update is not teleported to its blended position. This also changes bodies: a body turned in Update is now teleported from its true position, not from its blended one.
- Carried velocity does not exist yet (ticket 07), so a teleport has none to clear.

- [x] Tests: a position write teleports with no sweep through a wall between, refreshes `state` that tick, keeps `velocity`, and is left out of `movedVelocity`; a rotation-only write is not a teleport and blends.
- [x] Tests: a parented character is written back against its parent; moving the parent teleports it.
- [x] Tests: at frames of 0.3 ticks, position and rotation are the blend of the last two step poses within 1e-4; a teleport and a new character snap; `interpolate = false` and the app-wide switch turn blending off.
- [x] T snaps the character to the start; 2 toggles interpolation; keys run under `FENRIR_DEMO_KEYS=1` with no errors (checked by running).
