# 07: Platforms and carried velocity

**What to build:** A character rides whatever walkable ground it stands on, lifts, shuttles, rafts and rotating platforms with no game code, and keeps the ground's velocity when it walks or jumps off until it lands (spec: "FenrirPhysics: platforms and carried velocity", "the move in `PostTick`" (why before the step, the trim rule's frame)).

- **Ground velocity** is added privately to the move, never written into `velocity`: only after a move that ended `OnGround` (including the tick a jump starts), for any ground body (moving, rotating, kinematic or dynamic), read fresh before each move after this tick's `MoveKinematic` (`UpdateGroundVelocity`). Steep ground is not ridden.
- **State:** `groundVelocity` (the ground's velocity at the character) and `groundAngularVelocity`; physics never turns the character with a rotating platform.
- **Carried velocity:** on the first move that ends without walkable ground, the full ground velocity from the move before becomes the carried velocity. It is added to each move, trimmed against hits like `velocity`, and cleared on landing, on a teleport and on disable. Reported as `state.carriedVelocity`; included in `movedVelocity`.
- **The trim's frame:** trimming is measured relative to the ground the character rode, so a descending lift's own speed is never taken for falling.
- **Stated limit:** the hover above a descending lift (one tick of travel, `OnGround`; 1–4 airborne ticks where it turns from down to up) is documented, not fixed.
- **DemoApp:** the lift (1.5 m/s) and the shuttle (2 m/s); 3 pauses platforms (and later the paddle); readouts show "on ground: Lift" and `carriedVelocity`; the autopilot rides the lift and jumps off.

**Blocked by:** 03 (Character tracer bullet).

**Status:** resolved

Built test-first on branch `claude/character-platforms-b6a6de`; the user did the DemoApp hand check. Where the build settled something the ticket left open:

- Riding and carrying are the backend's, in `MoveCharacter`: it rides when Jolt's own ground state, from the last move or a teleport's refresh, is on ground, calling `UpdateGroundVelocity` first. The carried velocity goes in through `CharacterMove::carried` and comes back in `CharacterMoved::carriedVelocity` as what the next move carries (none on walkable ground, the whole ground velocity ridden on leaving it, otherwise trimmed). The pipeline keeps it in `CharacterRecord::moved`, and a teleport clears it there. Disabling (ticket 06) drops the record, so re-enabling starts with none (tested).
- The trim measures every contact in the frame of the ground ridden (surface velocity less the ground velocity added). Carried velocity is trimmed against each contact first, and game code's `velocity` takes what still goes into the surface, as the spike did.
- `state.groundVelocity` is Jolt's ground velocity at the end of the move (this tick's body velocities, at the character), so it is also set on steep ground, where it is reported but not ridden. `state.groundAngularVelocity` is the ground body's angular velocity.
- A test pins the stated limit: at an immediate down-to-up turn the character is off the lift for 3 ticks (at most 4 allowed).
- The DemoApp's lift waits at the bottom for boarding, goes up 3 m, down 1.5 m and at once up again (the turn that shows the hop), then down. The shuttle waits at its south end. The autopilot waits for each to begin its wait before boarding.

- [x] Tests: riding a lift up and down and a shuttle sideways, with `groundBody` naming it and no slipping; a jump off a lift rising at (1, 2, 0) m/s carries exactly that as `carriedVelocity`, none of it in `velocity`, cleared on landing.
- [x] Tests: riding a dynamic raft; riding a rotating platform without turning, with `groundAngularVelocity` reported; a teleport clears carried velocity; steep ground is not ridden.
- [x] Tests: standing on a descending lift never reads its speed as falling (`velocity.y` stays trimmed to 0).
- [x] Lift and shuttle ridden, a jump off the lift keeps `carriedVelocity` until landing, and the hover at the lift's turn shows as stated (checked by hand).
