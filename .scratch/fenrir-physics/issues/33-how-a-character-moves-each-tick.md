# How does a character move each tick?

Type: grilling
Status: resolved
Blocked by: 30, 32

## Question

Where does moving a character sit in the foundation's per-tick sequence ("How does physics step?": `PostTick` drains watches, diffs, applies forces, steps, writes back, sends events, makes debug lines), and what does a move do? Builds on [What does Jolt offer for characters?](30-what-jolt-offers-for-characters.md) and [How is a character body modelled in the ECS?](32-character-bodies-in-the-ecs.md).

- **Order:** characters moved before or after the world step, and what each order means for standing on bodies the step just moved.
- **Gravity:** applied by physics or by game code through the velocity.
- **Stairs and floors:** stepping up and sticking to the floor going down, on by default or opt-in.
- **Platforms:** inheriting the ground's velocity.
- **Interpolation:** whether characters interpolate like dynamic bodies (`RigidBody::interpolate`).
- **Teleports and edits** detected by the diff.
- **Determinism:** the order characters are moved in, and multithreaded stepping.
- **Inactive scenes:** the world pauses; confirm nothing new.

Facts from "What does Jolt offer for characters?": a character update cannot run during `PhysicsSystem::Update` or a step listener; Jolt's samples update before the step, its tests after. The host adds gravity and `GetGroundVelocity`. `ExtendedUpdate` does stairs (0.4 m) and stick-to-floor (0.5 m). Characters may update concurrently with separate temp allocators, but update order between characters changes results, and `CharacterID`s come from a process-global counter.

From "How is a character body modelled in the ECS?" (ADR-0014): a character moves once per tick in `PostTick` by `CharacterBody::velocity`, which game code writes in `Tick`; this ticket places that move before or after the step. Physics writes `velocity` back with the parts into what it hit removed, so gravity can live in `velocity` as game code's own addition; ground and platform velocity are never written into it, so riding a platform must come from somewhere else (added privately, or a separate field). Position only is written back, through `SetWorldTransform`, in world space even under a parent. A world pose differing from physics's copy is a teleport (no sweep, contacts and ground state refreshed, `velocity` kept); whether interpolation snaps follows `RigidBody::interpolate`'s rule, and whether `CharacterBody` has its own `interpolate` is this ticket's. Up is world +Y, so `ExtendedUpdate`'s Y-axis defaults hold. Slope, step and floor settings become live fields on `CharacterBody`; Jolt's construction-only settings stay private. A disabled or inactive character does not move.

## Answer

No ADR: every call here is private behaviour or a live field, and all of it fits ADR-0014. Glossary: **character body** amended (ground velocity added without changing game code's velocity), **carried velocity** added.

- **Where the move sits:** a new `PostTick` step **3b, "Move characters"**, after the diff (2), joint sync and queued pushes (3), and just before the step (4), as in Jolt's samples.
  - This tick's `MoveKinematic` has already set every platform's velocity, so a character and its platform move together in the same tick. The character's pushes and its weight on its ground act in this step.
  - A body the step then moves into the character may leave it slightly overlapping; the next move pushes it out.
  - Each character moves once per tick by the tick's `dt`, whatever the collision-step count.
  - Position and `velocity` are written back in step 5, beside dynamic bodies' write-back, parents before children.
- **Velocity write-back:** trimmed against the contacts the character touches at the end of the move: the part of `velocity` going into each surface, relative to that surface's own velocity, is removed. It is never measured from displacement, so a stair step-up or a floor snap never shows up in `velocity`. Landing clears falling speed, and a ceiling clears upward speed. How far the character actually moved is ticket 34's state.
- **Gravity:** game code's. It adds `PhysicsWorld::Gravity() * dt` to `velocity` each tick; physics adds no gravity to the movement and has no `gravityScale` or `useGravity`. Physics passes the world's gravity to the backend only for the character's weight on what it stands on. Because of the trim, a standing character's falling speed resets each tick instead of growing.
- **Slopes, stairs and floor sticking:** live `CharacterBody` fields, on by default, each switched off by 0:
  - `maxSlope = 0.785f` (45°, radians like the joints; Godot's, Rapier's and Jolt's samples' value, not Jolt's 50° default);
  - `stepHeight = 0.4f` (stair walking);
  - `floorSnap = 0.5f` (stick to the floor walking down slopes and steps).

  With no setting: velocity into a too-steep slope is cancelled, and only contacts on the lower part of the shape count as ground (set privately from the shape, as Jolt's samples set the supporting volume). Finer stair tuning (minimum step forward, forward test, extra step down) is private.
- **Standing on walkable slopes:** a live `bool stopOnSlope = true` (Godot's `floor_stop_on_slope`). While on walkable ground, the part of `velocity` going into the ground does not slide along it; turned off, characters slide (ice, slides).
- **Platforms:** ground velocity is added privately to the move, never written into `velocity`:
  - only after a move that ended on walkable ground (on ground, not steep or unsupported), including the tick a jump starts, so jumping off a rising lift keeps its speed;
  - for any ground body: moving, rotating, kinematic or dynamic;
  - physics never turns the character with a rotating platform; rotation is game code's.
- **Carried velocity:** on the first move that ends unsupported, the full ground velocity from the move before (horizontal and vertical) becomes the character's carried velocity. It is added to each move, trimmed against hits like `velocity`, and cleared on landing, on a teleport, and on disable. No setting; ticket 34 reports it.
- **Interpolation:** `CharacterBody` gains `bool interpolate = true` with `RigidBody::interpolate`'s rules, and `PhysicsConfig`'s app-wide switch covers it.
  - Position and rotation both blend between the last two steps (game code turns the character in `Tick`, like a kinematic body).
  - A stair step-up or floor snap blends: it is simulated movement.
  - A teleport, a new character and a re-enabled one snap.
- **What the diff treats as a teleport:** only a world *position* differing from physics's copy (including a moved parent): position set, contacts and ground refreshed, interpolation snapped, carried velocity cleared, `velocity` kept. This narrows ADR-0014's "writing the `Transform`" to its position. A rotation change alone is applied without a sweep, is not a teleport, and blends; the next move pushes the shape out of anything it now overlaps. Setting edits apply at the next move; `velocity` is read fresh every tick.
- **Order and threading:** characters move one at a time on the app thread, in ascending entity id (the diff's order), with one temp allocator; never in parallel, since order changes results. Privately, for repeatable runs: each character's backend ID comes from its entity, not Jolt's process-global counter; inner bodies are made in the same batch order as bodies; the hit limit stays high enough for contact sorting to hold. Parallel moves can come later behind a flag if profiling asks.
- **Inactive scenes:** nothing new. A paused world moves no character and changes neither `velocity` nor carried velocity; on resume characters carry on with no refresh. A disabled or inactive character skips step 3b and write-back; disabling clears its carried velocity, and re-enabling refreshes contacts as a teleport does.
- **Shape so far:**

  ```cpp
  struct CharacterBody
  {
      Math::Vec3 velocity = Math::Vec3(0.0f); // world space, m/s; written back trimmed by hits
      float maxSlope = 0.785f;                // radians; 0 turns slope limiting off
      float stepHeight = 0.4f;                // m; 0 turns stair walking off
      float floorSnap = 0.5f;                 // m; 0 turns floor sticking off
      bool stopOnSlope = true;
      bool interpolate = true;
      bool enabled = true;
      // strength and mass from ticket 35; read-back state from ticket 34
  };
  ```
