# Spike: a character walking a small level through the proposed model

Type: prototype
Status: resolved
Blocked by: 32, 33

## Question

Do the character model from [How is a character body modelled in the ECS?](32-character-bodies-in-the-ecs.md) and the per-tick move from [How does a character move each tick?](33-how-a-character-moves-each-tick.md) work in practice? Build a throwaway spike in the DemoApp on a `prototype/fenrir-physics-character-spike` branch, in its own worktree: a character driven from the keyboard through a small level with a walkable slope and a too-steep one, a flight of stairs, a moving kinematic platform and a few pushable boxes, made through the proposed public interface with Jolt behind the seam. Check it by hand in the DemoApp, as the earlier spikes were.

React to it: does the interface read well; does the ground state hold steady on slopes and stairs; does riding the platform feel right; does pushing boxes look right; does anything about Jolt leak. Record the lessons the spec needs.

The spike's scene may later become the C++ character demo scene (see "What do the character demo scenes show?").

From "How does a character move each tick?": prove the move as `PostTick` step 3b, before the step, in entity order: riding the kinematic platform (up, down and sideways; watch for Jolt's hover above a descending lift), walking and jumping off it with carried velocity, `velocity` trimmed against contacts (landing and a ceiling clear it; stairs and floor snaps never show in it), `stopOnSlope` holding still on the walkable slope, `maxSlope`/`stepHeight`/`floorSnap` at their defaults (45°, 0.4 m, 0.5 m), game code adding gravity, interpolated position and rotation at a frame rate above the tick rate, a teleport snapping, and two runs bit-identical with character IDs taken from entities.

## Answer

**Verdict: the character model works through the seam, and nothing about Jolt reaches the public interface.** The user ran the DemoApp's "Characters (spike)" scene (F5) and judged that it works. A capsule character walks a level with a 30° ramp, a 55° slope, ten 0.3 m stairs up to a 3 m ledge, a lift beside the ledge, a sideways shuttle, five 1 kg boxes and a low ceiling. Keys drive it, and a follow camera tracks it. The character's `Name` shows its ground state, `velocity`, carried velocity and ground changes per second, and each ground change is logged with its tick.

Prototype (primary source, never merged): branch `prototype/fenrir-physics-character-spike` (`78c2439`). The slice covers:

- `CharacterBody` with ticket 33's fields (`velocity`, `maxSlope`, `stepHeight`, `floorSnap`, `stopOnSlope`, `interpolate`, `enabled`) beside the entity's own `Collider`. That collider gets no static body.
- Inactive beside a `RigidBody`, with no `Collider`, or with a mesh shape (one warning).
- The move as `PostTick` step 3b, after the diff, joints and pushes and just before the step, in entity order. Position and `velocity` are written back in step 5.
- Ground velocity is added after a move that ended on walkable ground, then kept as carried velocity until landing.
- Interpolation of position and rotation, with snapping on teleport and re-enable.
- A stand-in read-back query, `PhysicsWorld::GetCharacterState` (ground, ground body, normal, ground velocity, carried velocity, moved velocity). This is a guess for ticket 34.

The headless check `FenrirPhysicsTests "[character-spike]"` has ten cases. The keys are WASD or the arrows, Shift, Space, Q/E, T, 1–5, L, P (autopilot) and C (free camera). `FENRIR_DEMO_KEYS=1` walks the autopilot route and presses every key.

**Where it guessed** (tickets 34–36 are open):

- the read-back state and its query (34);
- pushing is Jolt's default, with a strength of 100 N and a mass of 70 kg (35);
- no motion tests, and a shape, scale or filter edit remakes the character instead of reshaping it (36);
- child colliders under a character are ignored without the warning ADR-0014 asks for;
- `PhysicsMaterial` is ignored.

**Evidence:**

- **Seam:** Jolt is still included only in `JoltBackend.cpp`. The public headers gained `CharacterBody`, `CharacterGround`, `CharacterState` and `GetCharacterState`, and none of them names Jolt. The backend gained `CreateCharacter`, `DestroyCharacter`, `TeleportCharacter`, `TurnCharacter` and `MoveCharacter`.
- **Builds:** clang-x64-debug and clang-x64-release. All 245 FenrirPhysics test cases pass in both, the foundation's and the joints' included. MSVC and Linux were not built.
- **Headless checks and DemoApp smoke runs:**
  - **Landing:** a falling character lands, and its `velocity.y` stays within 1e-3 of zero for 60 ticks.
  - **Ceiling:** a jump into a ceiling 0.4 m above the head clears its upward speed. The head stops below the ceiling.
  - **Walkable slope:** with `stopOnSlope`, the character holds still on a 30° slope (under 1 mm in 2 s). With it off, it slides.
  - **Steep slope:** a 60° slope cannot be walked up. In the scene, the 55° slope cannot be climbed either.
  - **Stairs:** 0.3 m stairs (above the 0.3 m capsule radius, below `stepHeight`) are climbed by Jolt's stair step. 0.25 m stairs are rolled over by the capsule's round bottom. Climbing never stays on the ground less than every tick, and upward speed never shows in `velocity`.
  - **Platforms:** the lift is ridden up and down at 1.5–2 m/s and the shuttle sideways at 2 m/s. The character does not slip on the shuttle, and `groundVelocity` reads the platform's.
  - **Carried velocity:** a jump off a lift rising at (1, 2, 0) m/s carries exactly that velocity in the air, with none of it in `velocity`. Landing clears it.
  - **Pushing:** walking into a 1 kg box pushes it ahead.
  - **Interpolation:** at frames of 0.3 ticks, the shown position and rotation are the blend of the last two step poses, to within 1e-4. A teleport written in `Update` snaps.
  - **Determinism:** three characters bumping into each other and into four boxes, with a lift and jumps, gave bit-identical poses over 300 ticks in two runs in one process. Character IDs come from entities, not Jolt's global counter.

**Lessons for the character spec:**

1. **"Trim the part going into each surface" must not use the contact normal for what is underfoot.** Trimming walking velocity along a slope's or a step edge's normal turns it into upward speed in `velocity`. Game code keeps that speed, since it only adds gravity to `velocity.y`, so the character launches itself: 59 of 120 ticks were airborne on a staircase. The rule that worked:
   - A contact facing up (ground, a slope, a step's edge) takes only the falling part of `velocity`. On walkable ground with `stopOnSlope`, it takes all of the falling part. Otherwise it takes the falling part's component into the surface, so the rest slides the character downhill.
   - A wall or ceiling takes its into-surface part along its normal.
   - All of it is measured in the frame of the ground the character rode, so a descending lift's own speed is never taken for falling.

   With this rule, "landing and a ceiling clear it; stairs never show in it" holds. Ticket 33's wording should be amended.
2. **Rounded box edges read as steep slopes for 1–3 ticks.** FenrirPhysics rounds boxes by up to 0.05, and Jolt judges steepness from the surface normal, which on a rounded edge is the edge's own normal.
   - Going up a step, the character keeps moving at full speed, but about a tenth of the walking speed is trimmed from `velocity` for a tick.
   - Going down stairs, the ground state flickers between on ground and on steep ground at every step (2 ticks in 6 at 4 m/s).

   Ticket 34 must decide whether ground state is debounced or whether steep ground backed by walkable contact counts as ground.
3. **Jolt's hover above a descending lift is real, and visible at turnarounds.** Moving before the step, a character trails a descending platform by one tick of travel (2.5 cm at 1.5 m/s, 3.3 cm at 2 m/s) and stays on ground, as Jolt's docs warn. Where the lift turns from down to up, that gap leaves it in the air for 1–4 ticks, with ground-state events if ticket 34 sends them. Rising and sideways motion show no gap. The spec should state the hover. Removing it (a post-step follow for characters on kinematic ground) is a possible later refinement, not needed now.
4. **Velocity as input and output: sliding needs game code's cooperation.** With `stopOnSlope` off, game code that sets horizontal speed from input every tick throws away the slide's speed, so the character only creeps (about 0.1 m/s). Sliding works only when game code adds input to the written-back velocity. The spec and the C# docs (ticket 38) should say so, and the demo scene's game code should show it.
5. **No base-of-shape conversion is needed.** Jolt's character position can be the collider's centre. The rule "only the lower part of the shape is ground" is a supporting-volume plane at the capsule's cylinder bottom (generally `-bounds.min.y - radius`), set from the shape. ADR-0014's "physics converts to Jolt's base-of-shape position privately" can be dropped.
6. **The inner body needs no special care.** Its user data is the entity, so queries, contacts and triggers name the character without new code. The world's destructor must destroy characters before it removes all bodies, since each character destroys its own inner body.
7. **Details for the spec:**
   - The ground velocity must be read fresh before the move (Jolt's `UpdateGroundVelocity`), after the kinematic bodies' `MoveKinematic`. The cached value is a tick old.
   - `maxSlope = 0` already means off in Jolt (a cosine of 1).
   - The read-back state should carry the ground body. "Standing on the lift" was the most useful readout in the scene and in the tests.
   - FenrirPhysics's tick is fixed at 60 Hz and the DemoApp runs with vsync on, so blending shows by eye only on a display faster than 60 Hz. The headless check covers it at any frame rate.
