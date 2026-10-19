Status: resolved (built: PRs #28–#42 into dotnet-scripting)

# FenrirPhysics character bodies: walking, stairs, platforms, pushing and crouching, with motion tests, in C++ and C#

Origin: the FenrirPhysics wayfinder map (`.scratch/fenrir-physics/map.md`), whose character tickets (30–39) this spec collapses into one buildable plan. ADR-0014 (character bodies are a velocity-driven component, not a move call) records the hard-to-reverse call. ADR-0009 (engine packages extend scripting through extensions), ADR-0010 (the backend is hidden at compile time), ADR-0011 (physics components are plain data that physics diffs), ADR-0012 (each event reader sees each event once) and ADR-0013 (joints live on joint entities) still apply. A throwaway spike (`prototype/fenrir-physics-character-spike`) proved the model through the seam with a capsule walking a small level on F5. It builds on the foundation spec (`.scratch/fenrir-physics-foundation/spec.md`) and the joints spec (`.scratch/fenrir-physics-joints/spec.md`), whose `DemoScenes` switcher, writable entity links and `PostTick` steps it extends.

## Problem Statement

FenrirPhysics simulates rigid bodies and joints, but a game made with the engine has no way to walk a player through a level. A dynamic body rolls, tips over and slides down slopes; a kinematic body walks through walls. Game code cannot make a character that walks up a ramp but not a cliff, climbs stairs, sticks to the ground going down them, rides a lift, jumps off it with the lift's speed, pushes a crate, crouches under a beam and stands up only when there is room. It cannot ask what a character is standing on, in C++ or in C#.

Nor can game code build its own movement. The foundation deferred the motion-test queries: today's `ShapeCast` and `Overlap` report no safe distance and no penetration depth, so a custom mover cannot sweep a shape to a safe position or push it out of what it is in.

Characters bring their own traps, which other engines show:
- a native move call, made from any system in any order, moves characters outside physics's phase and in a non-deterministic order (Godot's "Hack" off the physics frame, Unity's one-`Move`-per-frame advice);
- ground state flickers between on and off the ground on slopes and stairs, the most common complaint everywhere;
- platforms jitter or are fallen through, and a character walking off one loses its speed;
- automatic pushing launches light bodies at many times the character's speed (Rapier);
- a character invisible to raycasts and triggers, or seen as two bodies (O3DE's bug);
- crouching with no check before standing up, so a character stands into a ceiling.

The scripting side cannot read a read-only field through `Ref<T>`, so a C# controller holding `Ref<CharacterBody>` to write its velocity could not read back its state without a second lookup.

## Solution

FenrirPhysics gains **character bodies**. A character body is a `CharacterBody` component beside the entity's own `Collider`. Game code drives it by writing its `velocity` field; physics moves it by that velocity once per tick, sliding along whatever is in its way, and writes the velocity back trimmed by what it hit. There is no move call.

Game code works the ECS way:
- **Moving:** write `velocity` in `Tick`. Gravity and jumping are part of it: game code adds `PhysicsWorld::Gravity() * dt` and sets `velocity.y` to jump. Landing clears falling speed and a ceiling clears upward speed.
- **Walking rules:** live fields for the slope limit (`maxSlope`, 45°), stair walking (`stepHeight`, 0.4 m), sticking to the ground going down (`floorSnap`, 0.5 m) and standing still on walkable slopes (`stopOnSlope`). Every setting is a live field edit; nothing ever recreates a character.
- **Ground state:** physics writes a `CharacterState` into the component after every move: on walkable ground, on steep ground, touching something with nothing underneath, or in the air; the ground's body, collider, point, normal and velocity; the velocity the character actually moved at; its carried velocity; whether it touches a wall or a ceiling; whether a shape change is blocked.
- **Platforms:** standing on walkable ground, a character moves with it, whatever kind of body it is. Walking or jumping off, it keeps the ground's velocity as its **carried velocity** until it lands.
- **Other bodies:** a character pushes the loose bodies it walks into with a capped force and presses down on what it stands on. Loose bodies never push it back; moving kinematic bodies shove it along. Other bodies, raycasts, shape casts, overlaps and triggers see it as a kinematic body of its own shape, naming its entity. Characters block each other.
- **Events:** the existing `ContactBegin`/`ContactEnd` (with `ContactEvents`) and `TriggerEnter`/`TriggerExit` name the character's entity. No new events; landing is a change in ground state game code notices.
- **Crouching:** edit the entity's `Collider` once. The new shape takes effect only once it fits; until then the character keeps the old one and reports `shapeBlocked`. On the ground the feet stay put; in the air the head does.

`PhysicsWorld` gains three general **motion tests**, for any shape: `MoveCast` (how far a shape can move and stay clear), `Penetrations` (what a shape overlaps, which way out and how deep) and `Depenetrate` (the offset that frees it). `QueryFilter` gains `layers`, so a query can filter exactly like a character.

Everything is available from C# through the physics scripting extension, with no native move or state function. FenrirScripting gains read-only fields readable through `Ref<T>`.

A new debug-line category, `Characters`, draws each character's shape in use, its ground normal and its moved velocity. The DemoApp gets two character scenes on F5, one built in C++ and one built in C#, each with a follow camera written in its own language.

## User Stories

### Using character bodies from C++

1. As a game developer, I want to make a character by adding a `CharacterBody` beside an entity's `Collider`, so that a character is a component like any other.
2. As a game developer, I want to move a character by writing its `velocity` each tick, so that I never call a move function and never worry about when it runs.
3. As a game developer, I want physics to write `velocity` back with the parts that went into what the character hit removed, so that landing clears falling speed and a jump into a ceiling stops rising.
4. As a game developer, I want gravity and jumping to be part of the velocity I set, so that I control them fully (low gravity, double jumps, gliding).
5. As a game developer, I want walking up stairs and sticking to the ground never to show up in `velocity`, so that walking up stairs never turns into a jump.
6. As a game developer, I want a slope limit, a step height and a ground-snap distance on by default, each a live field switched off by 0, so that a character walks a level without tuning.
7. As a game developer, I want a character to stand still on a walkable slope by default, and to slide when I turn that off, so that I can make ice slides and ramps.
8. As a game developer, I want sliding to keep its speed when my code adds input to the written-back velocity, so that a slide is not thrown away by my own steering.
9. As a game developer, I want every setting to be a live field edit applied on the next tick, so that tuning during play costs nothing and nothing recreates a character.
10. As a game developer, I want up to be world +Y whatever the shape's rotation, so that slope and stair rules never depend on how I turn the character.
11. As a game developer, I want the character's shape to take the entity's full world rotation, so that I turn it by writing its rotation, like any collider.
12. As a game developer, I want writing a character's position to teleport it, refreshing its ground state at once and keeping its velocity, so that respawning is a field write.
13. As a game developer, I want writing only a rotation not to be a teleport, so that turning a character every tick stays smooth.
14. As a game developer, I want a character's position and rotation interpolated between steps by default, with a field to turn it off, so that it moves smoothly at any frame rate.
15. As a game developer, I want a character under a parent to move in world space and be written back against its parent, so that the hierarchy is only organisation.
16. As a game developer, I want to switch a character off and on with a field, so that a cutscene can take over its `Transform` without a structural change.
17. As a game developer, I want a character beside a `RigidBody`, with no `Collider`, or with a mesh shape to go inactive with one warning and come back by itself once valid, so that mistakes are visible but never crash.
18. As a game developer, I want a solid collider on a child entity of a character to be ignored with one warning, and a trigger child to work, so that I know characters have one shape.
19. As a game developer, I want to read the ground state after each move (walkable ground, steep ground, touching, in the air), so that I can choose animations and allow jumps.
20. As a game developer, I want the ground's body, collider, point, normal, velocity and angular velocity, so that I can play footstep sounds by material, align feet and turn with a rotating platform.
21. As a game developer, I want the velocity the character actually moved at, including stairs, ground snapping, ground and carried velocity, so that animation speed matches movement.
22. As a game developer, I want to know when the character touches a wall or a ceiling, and to list its contacts with points and normals, so that I can wall-jump.
23. As a game developer, I want ground state never to name a stale ground after a character is disabled or made inactive, so that I never read a destroyed platform.
24. As a game developer, I want ground state to hold steady walking down stairs and over rounded edges, so that animations do not flicker.
25. As a game developer, I want a character to ride any ground it stands on, moving, rotating, kinematic or dynamic, so that lifts, shuttles and rafts work without code.
26. As a game developer, I want a character that walks or jumps off a moving platform to keep the platform's velocity until it lands, reported as its carried velocity, so that jumping off a moving lift feels right.
27. As a game developer, I want a character to push dynamic bodies with a capped force I can set, never downward, so that light boxes move and heavy crates do not.
28. As a game developer, I want a character's weight to press on what it stands on, with a mass I can set, so that it tips a seesaw or sinks a raft.
29. As a game developer, I want dynamic bodies never to push a character, so that a thrown ball cannot shove the player off a ledge unless my code says so.
30. As a game developer, I want kinematic bodies (doors, paddles) to shove a character at their own speed, so that moving machinery behaves predictably.
31. As a game developer, I want characters to block each other and to ride one another, so that crowds and piggyback work.
32. As a game developer, I want raycasts, shape casts, overlaps and triggers to see characters and name their entity, so that hit tests and trigger zones need no special case.
33. As a game developer, I want `QueryFilter::ignoreBody` to take a character, so that a character's own aim ray skips it.
34. As a game developer, I want the entity's `CollisionFilter` to filter both how the character moves and what sees it, under the one symmetric rule, so that teams and debris work with layers alone.
35. As a game developer, I want the existing contact and trigger events to name characters, including balls bouncing off a standing character, so that I handle characters as bodies.
36. As a game developer, I want a joint naming a character to go inactive with a warning, so that a mistake is visible and never crashes.
37. As a game developer, I want to crouch by editing the character's `Collider` once, so that changing shape is a field edit.
38. As a game developer, I want a stand-up that does not fit to wait, keeping the old shape and reporting `shapeBlocked`, until there is room, so that a character never stands into a ceiling and I never re-ask.
39. As a game developer, I want crouching on the ground to keep the feet down and crouching in the air to tuck them up, so that crouch-jumps clear ledges.
40. As a game developer, I want box, sphere, capsule and hull characters, with world scale applying, so that any convex shape and size works.
41. As a game developer, I want `MoveCast` to tell me how far any shape can move along a motion and stay clear, so that I can build a dash, a custom mover or a ledge check.
42. As a game developer, I want `Penetrations` to list what a shape overlaps, deepest first, with direction and depth, so that I can check for headroom and resolve overlaps.
43. As a game developer, I want `Depenetrate` to give the offset that frees a shape, so that I need not write an iterative resolve.
44. As a game developer, I want a query to filter exactly as a character does through `QueryFilter::layers`, so that custom moves agree with the character's own.
45. As a game developer, I want characters moved in the same order on every run, so that same-build determinism holds.

### Using character bodies from C#

46. As a script author, I want `CharacterBody` writable from C# with the C++ defaults, so that C# controllers drive characters exactly as C++ does.
47. As a script author, I want to read `body.State` through the same `Ref<CharacterBody>` I write `Velocity` through, and a compile error if I write it, so that state is cheap to read and safe.
48. As a script author, I want no native call to move a character or read its state, so that a controller costs nothing per character per tick.
49. As a script author, I want `MoveCast`, `Penetrations` and `Depenetrate` as native functions, and `QueryFilter.Layers`, so that I can build custom movement in C#.
50. As a script author, I want `ContactsOf` and the existing events to work for characters, so that wall jumps and trigger pads work in C#.
51. As a script author, I want the doc comments to say how sliding, being pushed, crouching and the moved velocity work, so that I avoid the traps of velocity as input and output.
52. As a script author, I want motion tests with no world to return nothing rather than throw, so that scripts are robust.

### Engine-wide

53. As an engine developer, I want read-only fields of writable components readable through `Ref<T>`, so that any component with a read-back part gets it for free.
54. As an engine developer, I want no Jolt type reachable from the character or motion-test interface, so that the backend stays replaceable.
55. As an engine developer, I want a `Characters` debug-line category, so that any renderer can show the shape a character really uses.
56. As an engine developer, I want the event and `ContactEvents` docs to say "body entity (a rigid body or a character body)", so that the documents stay true.

### DemoApp

57. As the engine developer, I want F5 to step Default → Character (C++) → Character (C#) → Default, so that character scenes switch like the joints scenes.
58. As the engine developer, I want the spike's level rewritten against the final API and extended with push masses, a seesaw, a paddle, a ball, a second character, a box on the head, a crouch tunnel and a crouch-jump ledge, so that I can check every rule by hand.
59. As the engine developer, I want a C# scene built entirely by a script, showing a slide, ice, a crouch tunnel, a dash, a push-out, a trigger pad, a lift and a landing counter, so that I can see the C# surface working live.
60. As the engine developer, I want each scene's follow camera written in its own language, driving the DemoApp camera through one demo component, so that the C# scene is C# end to end.
61. As the engine developer, I want the character's readings live in its entity name, so that I can watch ground state in the Scene Hierarchy.
62. As the engine developer, I want an autopilot in each scene, so that smoke runs, which can only tap keys, walk the level.

## Implementation Decisions

### Prerequisite: read-only fields through `Ref<T>` (FenrirScripting)

- For a `readonly` field of a writable component, `ComponentFieldsGenerator` also emits a `ref readonly` accessor on `Ref<T>`, beside the one it already emits on `ReadOnlyRef<T>`. A controller holding `Ref<CharacterBody>` to write `Velocity` can read `body.State.Ground` but not write it.
- Any later component with a read-only part gets it for free.
- Rejected: a writable `State` as in C++ (loses the guard), and documenting `body.Value.State` as the way to read it.
- A generator test covers it. It lands and is tested before the character extension work.

### FenrirPhysics: the `CharacterBody` component and `CharacterState` (ADR-0014)

- **`CharacterBody`** sits beside the entity's own `Collider`. Its shape is that collider's; the entity's `PhysicsMaterial` and `CollisionFilter` apply to it as to any body. A `Collider` on a character body's entity is not a static body.

  ```cpp
  enum class GroundState : std::uint8_t { InAir, OnGround, OnSteepGround, Touching };

  struct CharacterState                                  // written by physics after each move; game edits are overwritten
  {
      GroundState ground = GroundState::InAir;
      Entity groundBody;                                 // OnGround and OnSteepGround only
      Entity groundCollider;
      Math::Vec3 groundPoint = Math::Vec3(0.0f);         // world space
      Math::Vec3 groundNormal = Math::Vec3(0.0f);        // world space, unit, averaged over ground contacts
      Math::Vec3 groundVelocity = Math::Vec3(0.0f);      // m/s, the ground's velocity at the character
      Math::Vec3 groundAngularVelocity = Math::Vec3(0.0f); // rad/s, world space
      Math::Vec3 movedVelocity = Math::Vec3(0.0f);       // last move's displacement / dt
      Math::Vec3 carriedVelocity = Math::Vec3(0.0f);
      bool onWall = false;
      bool onCeiling = false;
      bool shapeBlocked = false;                         // the Collider's shape or scale doesn't fit yet
  };

  struct CharacterBody
  {
      Math::Vec3 velocity = Math::Vec3(0.0f); // world space, m/s; written back trimmed by hits
      float maxSlope = 0.785f;                // rad (45°); 0 turns slope limiting off
      float stepHeight = 0.4f;                // m; 0 turns stair walking off
      float floorSnap = 0.5f;                 // m; 0 turns sticking to the ground off
      float pushForce = 100.0f;               // N; the most force a push applies; 0 turns pushing off
      float mass = 70.0f;                     // kg; only the character's weight on its ground; 0 turns it off
      bool stopOnSlope = true;
      bool interpolate = true;
      bool enabled = true;
      CharacterState state;                   // read-back
  };
  ```

- **One kind of character:** collide-and-slide, always seen by bodies, queries and triggers as a kinematic body of its shape. No dynamic-body character (a dynamic `RigidBody` kept upright by game code covers that), and no invisible mode.
- **Units:** SI and radians, as `RigidBody` and the joints. No degree fields.
- **Live edits:** every field above except `state` is a live edit applied at the next move. `velocity` is read fresh every tick. Jolt's construction-only settings (padding, predictive distance, iterations, tolerances, back-face mode) are private tuning, never exposed, so nothing ever recreates a character.
- **`state`** is overwritten at every write-back (step 5). Game code's edits are ignored and overwritten; physics keeps no diff copy of it. It describes the end of the last move (step 3b), against the bodies' poses before that tick's step.
  - Rejected: a `PhysicsWorld` query in the joints' style (every controller reads ground state every tick, a native call per character per tick from C#), and a separate state component game code adds (one it can forget).
- **Inactive:** a character body is inactive beside a `RigidBody` (the `RigidBody` works), with no `Collider`, or with a `MeshShape`. Inactive means the component is kept, there is no backend character, one warning is logged on each transition from valid to invalid, and it comes back by itself once valid. It does not move, nothing sees it, nothing is written back, and its `state` is reset to the defaults.
- **No compounds:** a solid collider on a child entity with no `RigidBody` of its own is not part of the character, gets no body, and logs one warning. A child `Trigger` works as beneath any body. A character is never part of a parent `RigidBody`'s compound.
- **Disabled** (`enabled = false`): not moved, invisible to bodies, queries and triggers, `velocity` not written back, `Transform` owned by game code, `state` reset, carried velocity cleared. Re-enabling refreshes contacts and ground state as a teleport does and snaps interpolation.
- **Pose:** physics writes back only the position, through `SetWorldTransform`. The shape takes the entity's full world rotation; turning and keeping upright are game code's. The `Transform` sits at the collider's centre. Up is fixed at world +Y; the world's gravity direction does not turn the character.
- **Parents:** the character moves in world space. Its local `Transform` is written back against its parent's world transform, parents before children. Moving the parent teleports it. Platforms are ridden through ground velocity, not parenting.
- **No structural changes by physics:** physics only writes fields on components game code added; landing, leaving the ground or being blocked is never a structural change.
- **No landing or leaving events:** game code compares `state.ground` with its own copy from the last tick. Coyote time is a game-code timer. A `CharacterLanded` event can be added later without breaking anything.
- **Foundation wording:** where `ContactBegin`, `ContactEnd`, `TriggerEnter`, `TriggerExit`, `Contact` and `ContactEvents` say "rigid body entity", they now say "body entity (a rigid body or a character body)", in C++ and C#.
- **Documented** on `CharacterBody`: `velocity` is input and output, like `RigidBody::linearVelocity`; game code wanting the speed it asked for keeps its own copy, and `state.movedVelocity` is how fast the character actually moved. Sliding works only when game code adds input to the written-back velocity (spike lesson 4).

### FenrirPhysics: the move in `PostTick`

The foundation's `PostTick` sequence, as the joints spec left it, gains character work in steps 1, 2, a new 3b and 5. All sequential, on the app's thread.

1. **Drain** the `CharacterBody` watch with the body and joint watches, by each entity's net effect. Characters are destroyed before their bodies' batch and created after it; inner bodies are made in the same batch order as bodies. A joint naming a character goes inactive under the joints spec's edge-case rule.
2. **Diff** characters after bodies and before joints, in stable entity order: settings, `enabled`, teleports (a world position differing from physics's copy, including a moved parent), rotation, and the shape or world scale (applied in 3b, below).
3. Apply queued forces and impulses.
   - **3b, move characters:** one at a time, in ascending entity id, with one temp allocator: apply any pending shape change, then move each by `velocity` (plus ground or carried velocity) over the tick's `dt`, whatever the collision-step count. Pushes and weight on the ground are applied here and act in this tick's step.
4. Step.
5. Write back dynamic bodies, and characters' position, `velocity` and `state`, parents before children.
   - 5b, break overloaded joints, unchanged.
6. Sort and send contact, trigger and `JointBroken` events. Character contacts found in 3b merge into the same sort.
7. Regenerate debug lines if enabled.

- **Why before the step:** this tick's `MoveKinematic` has already set every platform's velocity, so a character and its platform move together in the same tick, and a character's pushes act in the step that follows (Jolt's samples' order). A body the step then moves into a character may leave it slightly overlapping; the next move pushes it out.
- **Velocity write-back:** `velocity` is trimmed against the contacts the character touches at the end of the move, relative to each surface's own velocity. It is never measured from displacement, so a stair step-up or a ground snap never shows in it. **The trim rule (spike lesson 1, decided in this spec):**
  - a contact facing up (ground, a slope, a step's edge) takes only the falling part of `velocity`: all of it on walkable ground with `stopOnSlope`, otherwise only the falling part's component into the surface, so the rest slides the character downhill;
  - a wall or ceiling takes the part of `velocity` going into it, along its normal;
  - all measured in the frame of the ground the character rode, so a descending lift's own speed is never taken for falling.

  Trimming walking velocity along an up-facing contact's normal is rejected: it turned walking into climbing speed (59 of 120 ticks airborne on a staircase in the spike).
- **Gravity** is game code's. Physics adds none to the movement and has no `gravityScale`; it passes the world's gravity to the backend only for the character's weight on its ground. Because of the trim, a standing character's falling speed resets each tick instead of growing.
- **Slopes, stairs, ground sticking:** with `maxSlope`, velocity into a too-steep slope is cancelled. `stepHeight` walks stairs; `floorSnap` sticks the character to the ground walking down slopes and steps. Finer stair tuning (minimum step forward, forward test, extra step down) is private.
- **Interpolation:** `CharacterBody::interpolate` follows `RigidBody::interpolate`'s rules, and `PhysicsConfig`'s app-wide switch covers it. Position and rotation both blend between the last two steps. A stair step-up, a ground snap and a crouch's centre shift blend; a teleport, a new character and a re-enabled one snap.
- **Teleports:** only a world *position* differing from physics's copy: no sweep, contacts and `state` refreshed at once, interpolation snapped, carried velocity cleared, `velocity` kept. A rotation change alone is applied without a sweep, is not a teleport, and blends; the next move pushes the shape out of anything it now overlaps. This narrows ADR-0014's "writing the `Transform`", which the first build ticket amends.
- **Determinism:** never in parallel, since order changes results. Privately: each character's backend ID comes from its entity, not Jolt's process-global counter; inner bodies are made in batch order; the hit limit stays high enough for Jolt's contact sorting to hold. Parallel moves can come later behind a flag if profiling asks.
- **Inactive scenes:** nothing new. A paused world moves no character and keeps `velocity`, carried velocity and `state`; on resume characters carry on with no refresh. Destroying a world destroys every character before any body (each character destroys its own inner body).

### FenrirPhysics: ground state

- **The four ground states:**
  - `OnGround`: standing on walkable ground (no steeper than `maxSlope`);
  - `OnSteepGround`: the ground underneath is steeper than `maxSlope`;
  - `Touching`: touching something (a wall, a ledge) with nothing underneath;
  - `InAir`: touching nothing.
- **Which contacts count as ground** is set privately from the shape, measured along world +Y: the shape's bottom within its rounding. For a capsule its lower hemisphere (a plane at `-bounds.min.y - radius`), for a sphere its lower half, for a box or hull its rounded bottom edge (their convex radius). Recomputed whenever the shape, scale or rotation changes.
- **The ground fields** are set only for `OnGround` and `OnSteepGround`, empty or zero otherwise. `groundBody` and `groundCollider` name entities as contact events do; a ground entity destroyed before game code reads it is detected by its version.
- **`movedVelocity`** includes stair step-ups, ground snaps, ground and carried velocity, but not a teleport (the move after one is measured from the new position) nor a crouch's centre shift.
- **Walls and ceilings:** `onWall` and `onCeiling` come from the end-of-move contacts the trim uses: a wall is a contact steeper than `maxSlope` that is not ground, a ceiling one whose normal points down. No normals in the state.
- **`PhysicsWorld::ForEachContact(character)`** lists a character's end-of-move contacts with point and normal (a wall jump's normal). Unlike a rigid body's, they come from before the step; the doc comment says so.
- **Against flicker:** `floorSnap` is the cling band; there is no grace period, debounce or air-time counter in physics. **Rounded edges (spike lesson 2, decided in this spec):** FenrirPhysics rounds boxes by up to 0.05 m, and judged by the contact normal a step's rounded edge reads as steep for 1–3 ticks, so ground state flickers between `OnGround` and `OnSteepGround` walking down stairs. Physics judges a ground contact's steepness by the surface normal of the face it touches, not the contact normal, privately, so a rounded edge over a walkable face counts as walkable ground; `groundNormal` reports the same normal. State stays a function of the last move only.
- **Reset** to the defaults when disabled or inactive; filled from that tick's move on a teleport tick; kept while paused.

### FenrirPhysics: platforms and carried velocity

- **Ground velocity** is added privately to the move, never written into `velocity`:
  - only after a move that ended on walkable ground (`OnGround`), including the tick a jump starts, so jumping off a rising lift keeps its speed;
  - for any ground body: moving, rotating, kinematic or dynamic;
  - read fresh before each move, after this tick's `MoveKinematic` (the backend's cached value is a tick old).
- **Rotation:** physics never turns the character with a rotating platform; `state.groundAngularVelocity` lets game code do it.
- **Carried velocity:** on the first move that ends without walkable ground, the full ground velocity from the move before (horizontal and vertical) becomes the character's carried velocity. It is added to each move, trimmed against hits like `velocity`, and cleared on landing, on a teleport and on disable. No setting. Reported as `state.carriedVelocity`.
- **Steep ground** is not ridden.
- **Stated limit, the hover above a descending lift:** moving before the step, a character trails a descending platform by one tick of travel (2.5 cm at 1.5 m/s) and stays `OnGround`, as Jolt documents. Where the lift turns from down to up the gap leaves it in the air for 1–4 ticks. Rising and sideways platforms show no gap. A post-step follow for characters on kinematic ground is a possible later refinement.

### FenrirPhysics: other bodies and the inner body

Privately, each active character is a Jolt `CharacterVirtual` with an **inner body**: a kinematic body of the character's shape, placed after each move with zero velocity, whose user data is the entity. Game code never sees it; it is how bodies, queries and triggers see the character.

- **Pushing dynamic bodies:** a move that hits a dynamic body gives it an impulse towards the character's speed at the contact point (aiming at 0.9 × the closing speed, so a light body is never launched faster than the character), clamped by `pushForce` and never downward. Static and kinematic bodies are never pushed. A 1 kg box keeps pace; a 20 kg crate with friction 0.2 accelerates at about 3 m/s²; a 100 kg crate with friction 0.5 does not move. No push reading in `state`: contact events and `ForEachContact` name what was touched.
  - Rejected: mass-ratio pushing (Unreal), a push callback (Unity, Avian), a maximum pushable mass (hides that friction matters).
- **Standing on bodies:** each move that ends on ground presses the ground body with `mass × gravity × dt` at the ground point (the world's gravity, scaled by how the ground normal faces it), so a character tips a seesaw or sinks a raft.
- **Stated limit, bodies on a character:** a body resting on a character (a box on its head) is not carried by friction, since the inner body has zero velocity; it slips or is pushed out. A jump still pushes it up.
- **Dynamic bodies never push a character:** in the step they meet the inner body as an immovable kinematic wall and stop or bounce. Knockback is game code writing `velocity`, from a contact event.
  - Rejected: recording step impacts and adding them to the next move (Unreal's mass-scaled shove).
- **Kinematic bodies shove a character** at exactly their own speed into it, whatever its mass: the move treats them as surfaces moving at their velocity, and under the trim that speed shows in the written-back `velocity` until game code overwrites it. Stated limits: a fast body may overlap the character for a tick until the next move pushes it out; crushing is not detected (the character stays overlapped and stuck; game code can find it from `onWall` and `ForEachContact`).
- **Joints:** a character is not a body a joint can name. A `Joint` naming a character entity is an inactive joint (component kept, one warning, back once valid).
- **Character against character:** characters block each other only through their inner bodies, found by the broad phase; Jolt's `CharacterVsCharacterCollision` (O(N²), not thread safe, each pair found twice) is not used. Neither pushes the other. In entity order a later character sees an earlier one's new position, the earlier one the later one's old position. A move sees another character's inner body moving at that character's last `movedVelocity`, so standing on a character rides it and following one behaves like a moving wall; the step still sees zero velocity.
- **Filtering:** the entity's `CollisionFilter` is both the inner body's filter and the move's, under the one symmetric rule: a character ignoring layer X walks through X's bodies, and X's bodies, triggers and queries masking it out pass through it. Layers cover teams, player against enemy and debris; general collision groups stay out of scope.
- **`PhysicsMaterial`** belongs to the inner body only: the friction and restitution other bodies feel when they hit or rest on the character. No material changes how a character moves; ice is game code reading `state.groundCollider`'s material and easing `velocity`. The spike ignored it, so it needs building.
- **Queries:** raycasts, shape casts and overlaps hit characters, naming the character's entity as body and collider, at the inner body's pose after the last step. `QueryFilter::ignoreBody` takes a character's entity and skips its inner body. Hits carry no character flag; game code checks the entity for `CharacterBody`.

### FenrirPhysics: contact and trigger events

- **Contact events:** the existing `ContactBegin`/`ContactEnd`, the character's entity as both body and collider, opt-in through `ContactEvents` on either side. No new events.
  - With static and kinematic bodies and other characters, contacts come from moves only: begins and ends from comparing each move's end-of-move contacts with the previous move's; `relativeSpeed` is the closing speed along the normal at the start of the move.
  - With dynamic bodies, contacts are the union of the move's end-of-move contacts and the inner body's step contacts (a ball can bounce off and be gone before the next move): touching while either source has the pair, `ContactBegin` from whichever sees it first (with that source's `relativeSpeed`), `ContactEnd` when neither has it.
  - Still one pair per entity pair, merged into step 6's sort by (entity A, entity B, collider A, collider B); character against character is one pair ordered by entity.
  - A character disabled, made inactive or losing its `CharacterBody` ends each reported contact on the next step (naming the possibly stale entity), as a removed body does, so every begin has an end; likewise when the other side drops `ContactEvents`.
- **Triggers:** a character sets off `TriggerEnter`/`TriggerExit` like any body, during the step, through its inner body, with `otherBody` and `otherCollider` both its entity. Disabling, inactivity or removal exits its triggers on the next step.

### FenrirPhysics: motion-test queries

General `PhysicsWorld` queries, not character-only. They take any `CollisionShape` at any pose (scaled as a collider is), take a `QueryFilter`, see the last completed step, never allocate, and are safe from parallel systems. Given a `MeshShape` they find nothing, as `ShapeCast` already does; the doc comments say so. A character still moves only through `velocity`; game code may teleport it to a pose a motion test found safe.

```cpp
struct MoveCastHit { Entity body; Entity collider; Math::Vec3 point; Math::Vec3 normal; float distance; };
struct PenetrationHit { Entity body; Entity collider; Math::Vec3 point; Math::Vec3 normal; float depth; };

[[nodiscard]] std::optional<MoveCastHit> MoveCast(const CollisionShape& shape, const Transform& pose,
                                                  const Math::Vec3& motion, const QueryFilter& filter = {}) const;
std::size_t Penetrations(const CollisionShape& shape, const Transform& pose, std::span<PenetrationHit> hits,
                         const QueryFilter& filter = {}) const;
[[nodiscard]] Math::Vec3 Depenetrate(const CollisionShape& shape, const Transform& pose,
                                     const QueryFilter& filter = {}) const;
```

- **`MoveCast`:** `motion` is a displacement. `distance` is the **safe** distance: moved that far, the shape stays a small gap (the skin) from what blocked it; no hit means the whole motion is clear. `point` is where the shape touches the body, `normal` the body's surface normal towards the shape. It ignores bodies the shape starts inside or is moving away from, so a mover resting on the ground is not stuck at distance 0. Closest hit only; `ShapeCastAll` already lists everything along a path.
  - The skin is fixed and private at the character's padding (0.02 m).
  - Rejected: a safe distance on `ShapeCastHit` (changes what its distance 0 means); a `skin` parameter (leaks tuning).
- **`Penetrations`:** `point` the deepest point of the overlap, `normal` the unit direction out along the shortest path, `depth` how far along it. Deepest first; with more overlaps than `hits` holds, the deepest are written. Sorted, since the backend's arrive in a varying order. `Overlap`, `OverlapPoint` and `OverlapHit` are unchanged.
- **`Depenetrate`:** the offset that moves the shape out of everything it overlaps, resolving several overlaps together over a few private iterations; zero when nothing overlaps. In a crevice it may be partial; the doc comment says to check with `Penetrations` when it matters.
- **`QueryFilter` gains `layers`,** between `mask` and `ignoreBody`:

  ```cpp
  struct QueryFilter
  {
      std::uint32_t mask = ~0u;
      std::uint32_t layers = 0u; // when non-zero, a body is found only if its CollisionFilter mask includes one; 0 ignores masks
      Entity ignoreBody = Entity();
      bool hitTriggers = false;
  };
  ```

  - 0 keeps today's behaviour for every query, so a ray still finds a hitbox that collides with nothing.
  - Filtering a custom move like a character is `{ .mask = cf.mask, .layers = cf.layers, .ignoreBody = character }`, with `cf` its `CollisionFilter` or the defaults. No helper; the doc comments on `layers` and `MoveCast` show this line.

### FenrirPhysics: shape changes and crouching

- **Allowed shapes:** box, sphere, capsule and convex hull. A `MeshShape` makes the character inactive.
- **Scale:** the entity's world scale applies to a character's shape exactly as to any collider. A scale edit is a shape edit.
- **A shape edit** (the `Collider`'s shape or the entity's world scale) is applied in step 3b, just before that tick's move, through the backend's checked reshape at a private penetration limit (Jolt's sample uses 1.5 × the penetration slop):
  - **it fits:** the outer shape and the inner body switch together (both or neither), contacts and `state` refresh, and the move runs with the new shape;
  - **it doesn't fit:** physics keeps the old shape and tries again at every move until it fits. Bodies, queries, triggers and debug lines see the old shape meanwhile. `state.shapeBlocked` is true while it waits. The `Collider` keeps holding the shape game code asked for; physics never writes the `Collider` back. The diff copy holds the shape in use, so the retry is the same comparison every tick.
- **What blocks:** any solid body the character's `CollisionFilter` collides with, including other characters and loose boxes. Triggers never block.
- **Game code** writes the standing shape once when crouch is released, and the character stands as soon as there is room. A headroom check ahead of time is `Penetrations` or `Overlap` with the standing shape at the pose it would have.
- **Where the centre goes:** on ground (`OnGround` or `OnSteepGround` after the last move) the shape's lowest point along +Y stays put: crouching lowers the head, standing grows upward. In the air (`InAir` or `Touching`) its highest point stays put: crouching mid-jump tucks the feet up, standing in the air extends them down (refused while the ground is in the way). The shift is written back with the position, blends, is not a teleport, keeps `velocity` and carried velocity, and is left out of `movedVelocity`.
- **Nothing else follows the shape:** `stepHeight`, `floorSnap`, `maxSlope`, `pushForce` and `mass` stay as game code set them. A rotation-only change stays unchecked.
- Rejected: applying the shape anyway and letting the move push out (can push through thin floors); writing the old shape back into the `Collider`; game code shifting the `Transform` itself (a teleport).

### FenrirPhysics: the Jolt backend (private)

All in `src/Backend/Jolt/`; no public header names Jolt.
- The backend class gains character operations (in the spike: `CreateCharacter`, `DestroyCharacter`, `TeleportCharacter`, `TurnCharacter`, `MoveCharacter`), plus a checked reshape (replacing the spike's remake on a shape edit) and the three motion tests.
- **The move** is `CharacterVirtual::ExtendedUpdate` (stairs and sticking to the ground), with the trim rule above applied in Jolt's contact-solve hook, `UpdateGroundVelocity` before each move, and `OnAdjustBodyVelocity` reporting another character's inner body at its last `movedVelocity`.
- **Position:** the character's position is the collider's centre; no base-of-shape conversion is needed (spike lesson 5; the first build ticket drops ADR-0014's base-of-shape statement). The ground rule is a supporting-volume plane set from the shape.
- **IDs:** `CharacterID` from the entity, not Jolt's global counter; the inner body's `BodyID` pinned through `mInnerBodyIDOverride` in batch order. `mMaxNumHits` kept high enough for sorted contacts.
- **Filters:** each move is filtered through the inner body's own object layer from FenrirPhysics's private layer mapping, with the trigger broad-phase tree excluded from the move (triggers see the character through its inner body).
- **Pushing and weight:** Jolt's max strength is `pushForce`, its mass `mass`; the world's gravity is passed for the weight only.
- **Contacts:** end-of-move contacts from `GetActiveContacts`, sorted; the inner body's step contacts with dynamic bodies from the body contact listener, unioned per pair.
- **Ground steepness** judged from the touched face's surface normal, not the contact normal.
- **Motion tests:** `MoveCast` is Jolt's `ValidateMovement` pattern rebuilt on `CastShape` (a fraction limit past 1 by the skin, hits at fraction 0 and hits moving away dropped, the fraction backed off by the skin); `Penetrations` is `CollideShape` with penetration axis and depth, sorted; `Depenetrate` iterates over it.
- **World destruction** destroys characters before it removes all bodies.

### FenrirPhysicsScripting and Fenrir.Physics

- **`Fenrir.Physics` contract:**
  - a new `Characters.cs`: `[NativeComponent("Fenrir.Physics.CharacterBody")] public struct CharacterBody : IComponent` with C++'s fields in order and PascalCase and C++'s defaults (`Vector3 Velocity`; `float MaxSlope = 0.785f, StepHeight = 0.4f, FloorSnap = 0.5f, PushForce = 100, Mass = 70`; `bool StopOnSlope = true, Interpolate = true, Enabled = true`; `public readonly CharacterState State`); `enum GroundState : byte`; `CharacterState` as a `readonly struct` with `readonly` fields (`EntityLink GroundBody, GroundCollider`, `EntityLink.None` when new);
  - `MoveCastHit` and `PenetrationHit` in `Queries.cs` as plain structs with plain `Entity` fields, like `ShapeCastHit`; `QueryFilter` gains `public uint Layers;` between `Mask` and `IgnoreBody`, matching C++'s layout;
  - on `PhysicsWorld`, in the foundation's query pattern:

    ```csharp
    public static bool    MoveCast(CollisionShape shape, Transform pose, Vector3 motion, out MoveCastHit hit, QueryFilter? filter = null);
    public static int     Penetrations(CollisionShape shape, Transform pose, Span<PenetrationHit> hits, QueryFilter? filter = null);
    public static Vector3 Depenetrate(CollisionShape shape, Transform pose, QueryFilter? filter = null);
    ```

  - three `PhysicsNative` entries; `Depenetrate`'s is `bool Depenetrate(in shape, in pose, in filter, out Vector3 offset)`, shaped like `Gravity`;
  - the "body entity (a rigid body or a character body)" wording in `Events.cs`, `Queries.cs` and `Components.cs`.
- **No native move or state function** (ADR-0014). A C# `Tick` system writes `Velocity` through `Ref<CharacterBody>`, adds `PhysicsWorld.Gravity * dt`, jumps by setting `Velocity.Y`, teleports by writing the `Transform` position, crouches by writing `Collider.Shape` once, and reads `body.State` in place.
- **No convenience members** such as `IsGrounded`; C# matches C++. **No filter sugar:** `new QueryFilter { Mask = cf.Mask, Layers = cf.Layers, IgnoreBody = me }`, shown in doc examples. Rejected: a C#-only `QueryFilter.Like(Entity)`.
- **Doc comments** carry the behaviour notes: sliding (add input to the written-back `Velocity`); being pushed (dynamic bodies never push, kinematic bodies shove at their own speed into `Velocity`); crouching (feet down on ground, head in place in the air, `StepHeight` does not follow, `State.ShapeBlocked` until the stand-up fits); actual speed (`State.MovedVelocity`). `PhysicsWorld.ContactsOf` says a character's contacts are its end-of-move contacts, from before the step.
- **`default(CharacterBody)`** is all zeros: a disabled character with every rule off, and no warning. Its doc comment says to make it with `new`.
- **Robustness:** only the foundation's rules. With no physics world, `MoveCast` returns `false`, `Penetrations` 0 and `Depenetrate` zero, each with the debug warning once per native site. Disabled and inactive characters are invisible to queries and their `State` reads as defaults. Nothing throws or makes a structural change. These go in `PhysicsWorld`'s remarks.
- `CharacterState`'s bools are read in place and it never crosses from C++ by value, so the README's bool caution does not apply.
- **`FenrirPhysicsScripting`:** the `CharacterBody` mapping with an `EntityLinkView` stand-in for each ground entity, the `QueryFilter` mirror gaining `layers`, and the three native functions. FenrirPhysics itself still names nothing from Iris.

### DemoApp: the two F5 scenes and the follow camera

- **Switching:** F5 steps Default → "Character (C++)" → "Character (C#)" → Default through `DemoScenes`. Without scripting F5 toggles Default ⇄ Character (C++). Each scene logs its key help on entry and registers its smoke keys.
- **Camera:** a demo component `DemoCamera { Vec3 Position; float Yaw; float Pitch; int Active; }` (`Demo.DemoCamera`), registered in the demo's scripting extension, on an entity in each scene.
  - "Character (C++)": a C++ `CharacterCamera` controller writes it. "Character (C#)": a C# `CharacterCamera` system does the same job.
  - One C++ system, `DemoCameras`, copies the active scene's `DemoCamera` into `Fenrir::Camera` while `Active` is set. It runs after `CameraController` and after the C# Update systems, so it wins (`DemoMesh`'s pattern).
  - C writes `Active = 0`, leaving the camera to the free-fly `CameraController` (WASD) while the arrows still walk; C again snaps back. Q/E orbit. Walking is relative to the camera's yaw.
  - Rejected: one C++ follow camera following a marker C# places.
- **Game code steers velocity:** both scenes move the written-back horizontal `velocity` toward `input × speed` by at most `acceleration × dt` each tick, so a `stopOnSlope = false` slide keeps its speed and continues with no input.
- **Readouts:** the character's `Name` shows every frame the ground state and ground body ("on ground: Lift"), `velocity`, `movedVelocity`, `carriedVelocity`, `onWall`/`onCeiling`/`shapeBlocked` while set, the landing count (from comparing with last tick) and ground changes per second. Each ground change is logged with its tick; L logs the full `state`. Push boxes show mass and speed, the seesaw its angle, the second character its own ground state.
- **"Character (C++)"**, the spike's `CharacterScene.hpp` rewritten against the final API (reads `body.state`, reshapes instead of remaking, drops the spike's guesses):
  - **keeps** the 30° ramp, the 55° slope, ten 0.3 m stairs to a 3 m ledge, the lift (1.5 m/s), the shuttle (2 m/s) and the low ceiling;
  - **adds** boxes of 1, 20 and 100 kg; a hinged seesaw; a kinematic paddle sweeping a lane; a ball thrown at the standing character (it has `ContactEvents`, so the bounce's `ContactBegin` is logged); a second character on a C++ loop, which blocks the player and, stopped, can be stood on and ridden; a 1 kg box dropped on the head; a crouch tunnel with a box over a roof gap and a trigger volume; a crouch-jump ledge;
  - **keys:** WASD/arrows walk, Shift run, Space jump, Q/E orbit, C free camera, X toggle crouch, T teleport to start, 1 `stopOnSlope`, 2 `interpolate`, 3 pause platforms and paddle, 4 `enabled`, 5 stairs and ground snap, 6 stop or start the second character, B throw the ball, H drop the box, G scale by 1.5× and back, M swap to a mesh collider and back, L log `state`, P autopilot.
- **"Character (C#)"**, built only from C#; C++ places a `CSharpScene` marker with id 3 (`CSharpScenes::characterId`):
  - a capsule character with a `Collider`; a controller adding gravity from `PhysicsWorld.Gravity`, jumping and steering the written-back `Velocity`;
  - a slide ramp (`StopOnSlope = false` at the start); a crouch tunnel reading `ShapeBlocked`; an ice patch whose friction, read from `GroundCollider`'s `PhysicsMaterial`, lowers the acceleration cap; a trigger pad logging `TriggerEnter` naming the character; one kinematic lift so `CarriedVelocity` shows; a landing counter;
  - **keys:** the same letters for the same jobs as C++ (WASD/arrows, Shift, Space, Q/E, C, X, T, L, P, and 1 toggling `StopOnSlope`), plus F dash (`MoveCast` ahead filtered with `Layers`, teleport to the safe distance, log the hit's name and distance) and K push out (teleport half into a test wall, log `Penetrations`' count and deepest depth, apply `Depenetrate`);
  - it does not repeat the C++ scene's push masses, paddle, ball, second character, scale or mesh keys.
- **Crouch is a toggle** and **each scene has a P autopilot**, because smoke runs can only tap. The C++ route extends the spike's through the boxes, seesaw, paddle lane and tunnel (toggling crouch off inside, so `shapeBlocked` shows); the C# route takes the slide ramp, ice, tunnel, trigger pad and lift. Each smoke run presses P, about 25 L presses, P again, taps each scene key twice, F and K once, and finally T.
- **Interpolation:** both scenes keep the `interpolate` key; seeing it is not a hand check on a 60 Hz display, and the demo keeps vsync.
- The F2 cycle gains "characters" before "all".

### FenrirPhysics: debug lines

- `DebugDraw::Characters`, a new category included in `All`, with a C# mirror. For each active character it draws:
  - **the shape in use** as a wireframe at its step pose, in a new `Character` colour: the crouched shape while a stand-up is blocked, and the shape the inner body presents to bodies and queries;
  - **the ground normal** from the ground point, coloured by ground state;
  - **a `movedVelocity` line** as far as the character travels in a tenth of a second, like `Velocities`.
- Inactive and disabled characters draw nothing. `Colliders` still skips characters.

## Testing Decisions

- **What a good test is:** as in the foundation. It drives the engine through its public interface and asserts on component values (`velocity`, `state`), world transforms, events read, query results and debug lines. Never on private state or which backend calls were made. Tests run against real Jolt. Same-build determinism lets tests assert exact results; resting heights and slides use a tolerance.
- **Seam 1, the App stepped one frame at a time with a chosen delta** (`FenrirPhysicsTests`):
  - the component: defaults; inactive beside a `RigidBody`, with no `Collider`, with a mesh, with one warning per transition and reactivation; a solid child collider warned and ignored, a trigger child working; disabled characters unmoved, unseen and with `state` reset; re-enabling refreshing as a teleport;
  - the move: landing clears `velocity.y` (within 1e-3 for 60 ticks); a ceiling clears upward speed; the trim rule on a 30° slope, a 60° slope and 0.3 m stairs (never airborne climbing, no upward speed in `velocity`); `stopOnSlope` holding under 1 mm in 2 s and sliding when off; `maxSlope`, `stepHeight` and `floorSnap` each switched off by 0; a rotation write not a teleport; a position write teleporting, keeping `velocity` and clearing carried velocity; a parented character written back against its parent;
  - ground state: each of the four states; ground fields only with ground; `movedVelocity` including stairs and carried velocity but not a teleport or crouch shift; `onWall`, `onCeiling`; `ForEachContact` for a character; no `OnSteepGround` walking down rounded-edged stairs;
  - platforms: riding a lift up and down and a shuttle sideways with `groundBody` naming it; a jump off a lift rising at (1, 2, 0) m/s carrying exactly that, none in `velocity`, cleared on landing; riding a dynamic raft and a rotating platform without turning;
  - other bodies: a 1 kg box pushed at pace, a 100 kg box with friction 0.5 unmoved, `pushForce = 0` pushing nothing; a seesaw tipped by `mass`; a ball bouncing off a standing character without moving it, with one `ContactBegin`; a kinematic paddle shoving at its own speed; two characters blocking, one riding the other; a joint naming a character inactive; `CollisionFilter` passing a character through a masked layer both ways; `PhysicsMaterial` felt by a box resting on a character; queries naming the character and `ignoreBody` skipping it;
  - events: contact begins and ends from moves, the dynamic-body union, one pair per entity pair, sorted with body contacts; ends on disable, inactivity and removal; triggers entered and exited by a character;
  - shape changes: a crouch fitting and lowering the head on ground, tucking the feet in the air; a stand-up under a box blocked with `shapeBlocked`, the `Collider` unchanged, and standing by itself once the box is removed; a trigger not blocking; scale edits checked the same way; the centre shift not a teleport and not in `movedVelocity`;
  - motion tests: `MoveCast`'s safe distance against a wall (the skin's gap), clear motion returning nothing, a shape resting on the ground not stuck at 0; `Penetrations` deepest first and truncated to the deepest; `Depenetrate` freeing one and two overlaps, zero when clear; meshes finding nothing; `QueryFilter::layers` filtering by bodies' masks and 0 keeping today's results for every foundation query;
  - interpolation: at frames of 0.3 ticks, position and rotation are the blend of the last two step poses within 1e-4; teleport and re-enable snap;
  - determinism: three characters bumping into each other and four boxes, with a lift and jumps, bit-identical over 300 ticks in two runs, single- and multi-threaded;
  - paused scenes changing nothing; destroying a world with characters;
  - `Characters` debug lines: counts for a known character, the blocked shape drawn while `shapeBlocked`, nothing for inactive ones.

  Prior art: the foundation's `PhysicsTestApp` and `PhysicsProbe`, the joints' cases, and the spike's `[character-spike]` cases on the prototype branch.
- **Seam 2, the scripting fixture harness:**
  - in the generator tests: a `readonly` field of a writable component gets a `ref readonly` accessor on `Ref<T>`, and writing it does not compile;
  - in `PhysicsScriptingTests` with a `Characters.cs` fixture: `CharacterBody`, `MoveCastHit`, `PenetrationHit` and the new `QueryFilter` validate against their native layouts; `new CharacterBody()` equals C++'s `CharacterBody{}`, including the empty ground links; a C# system makes a character, adds gravity, lands it and reads `State.Ground == OnGround` with `GroundBody` naming the floor; writing `Velocity` moves it; a crouch writes `Collider` once, `ShapeBlocked` is true under a box and clears once it is gone; the three motion tests match C++ for the same scene and return false, 0 or zero with no world; `Layers` filters as in C++; `ContactBegin` read in C# names the character.

  Prior art: the foundation's and joints' `PhysicsScriptingTests` and fixtures, `ScriptingProbe`, `HeadlessApp`, and the existing `ComponentFieldsGenerator` tests.
- **FenrirECS:** nothing new; the character watch uses the foundation's component watches.
- **The DemoApp scenes are verified by hand,** as the spikes were:
  - **Switching and smoke runs:** F5 steps Default → Character (C++) → Character (C#) → Default, logging key help; the F2 mode carries across, and "characters" draws the shape, ground normal and `movedVelocity`; `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs each scene through its autopilot and keys with no errors, in Debug and Release; without scripting F5 toggles Default ⇄ Character (C++).
  - **Camera (both):** follows; Q/E orbit; C frees it (WASD flies, arrows walk) and C snaps back.
  - **Character (C++):** the ramp walked, the 55° slope not climbed and reading "on steep ground", with 1 off a slide at full speed; stairs climbed with no airborne ticks and descended with no steep flicker, blocking with 5 off; lift and shuttle ridden ("on ground: Lift"), a jump off the lift keeping `carriedVelocity` until landing, the hover at the lift's turn visible as stated; the low ceiling stopping a jump with `onCeiling`; 1 and 20 kg boxes moving and 100 kg not; the seesaw tipping; the paddle shoving at its own speed; B's ball bouncing off with its `ContactBegin` logged; the second character blocking, and with 6 stood on and ridden ("on ground: Character"); H's box left behind; the tunnel's crouch, `shapeBlocked` until the exit, the roof-gap box blocking and the trigger not; the crouch-jump clearing the ledge; G walking with the larger shape shown by the lines; M giving one warning, inactive and drawing nothing, and back; 4 off letting boxes pass through and on resuming where it stands; T snapping to the start; the readouts live.
  - **Character (C#):** builds with no warning; the C# camera follows, the character walks, jumps and slides down the ramp; slow to start and stop on ice; the tunnel as in C++; F stopping short and logging the hit; K logging penetrations and resolving out; the trigger pad logging `TriggerEnter` naming the character; the lift showing `CarriedVelocity`; the landing counter rising once per landing.

## Out of Scope

- Input mapping, the camera beyond the demo's own follow cameras, root motion, AI navigation and pathfinding, and network prediction.
- A native move call (ADR-0014), and a dynamic-body character.
- Compound characters, triangle-mesh characters, and a per-character `up` direction (addable later as a field defaulting to +Y).
- Landing and leaving events, coyote time, ground-state debouncing or air-time counters in physics.
- Characters as joint ends; grabbing and carrying.
- Being pushed by dynamic bodies (knockback is game code), mass-ratio pushing, push callbacks, and crush detection.
- Carrying bodies resting on a character.
- General collision groups: with ragdolls and vehicles.
- A post-step follow to remove the hover above a descending lift.
- Parallel character moves.
- Materials changing how a character moves (ice is game code).
- `MoveCastAll`, a skin parameter, filter helpers in either language.
- Vehicles, ragdolls and soft bodies: each its own spec.
- A separate demo app per area, and an on-screen text overlay in the DemoApp.

## Further Notes

- **Decided in this spec** (spike lessons no later ticket picked, answered by the user when the spec was reviewed, 2026-10-18):
  1. **The velocity-trim rule** (spike lesson 1) is the spike's amended rule, in "The move in `PostTick`": up-facing contacts take only the falling part (all of it on walkable ground with `stopOnSlope`, its into-surface component otherwise), walls and ceilings their into-surface part along the normal, all relative to the ridden ground. It replaces ticket 33's "the part going into each surface", which along an up-facing contact's normal turned walking into climbing speed. Every later ticket's examples (landing, ceilings, the kinematic shove) hold under it.
  2. **Ground-state flicker on rounded edges** (spike lesson 2): physics judges a ground contact's steepness by the touched face's surface normal, not the contact normal, so a rounded edge over a walkable face is walkable ground. It keeps state a function of the last move with no hidden timer, and also removes the tenth of walking speed trimmed for a tick going up a step. Rejected: accepting the flicker, and debouncing over ticks. This sharpens ticket 34's "`floorSnap` only" and replaces ticket 39's "at most a brief steep flicker" in the hand check. If building shows Jolt's ground state cannot be fed the face normal, the ticket says so and asks before falling back to accepting the flicker.
  3. **ADR-0014 is amended by the first build ticket,** as `CODING_STANDARDS.md` asks of a change that makes an ADR false: a teleport is a change of world position only (ticket 33; the ADR was left unchanged when that ticket was answered), and there is no base-of-shape conversion (spike lesson 5).
- **Decision history:**
  - the wayfinder map `.scratch/fenrir-physics/map.md` and its character tickets 30–39;
  - ADR-0014, with ADR-0009, ADR-0010, ADR-0011, ADR-0012 and ADR-0013 still applying;
  - research branches `research/jolt-characters` (`2953281`) and `research/engine-character-models` (`53ccebe`);
  - the spike branch `prototype/fenrir-physics-character-spike` (`78c2439`).
- **Where tickets disagreed,** the later one is followed:
  - contacts with dynamic bodies are the union of move and inner-body step contacts (35, over 34's moves only);
  - a teleport is a position change only (33, over ADR-0014's `Transform` write);
  - state is written into `CharacterBody`, not read by `GetCharacterState` (34, over the spike's guess);
  - a shape edit reshapes with a room check (36, over the spike's remake);
  - `QueryFilter.Layers` sits between `Mask` and `IgnoreBody` in both languages (38);
  - the lift hover is a stated limit, visible in the hand checks (39, settling lesson 3);
  - sliding needs game code to add input to the written-back velocity, in the docs and the demo (38 and 39, settling lesson 4).
- **Risks:**
  - The trim rule lives in Jolt's contact-solve hook and interacts with stairs, ground snapping and platforms; test each early against the spike's cases.
  - The contact union for dynamic bodies needs the inner body's step contacts from the body contact listener on job threads, buffered and merged with move contacts; the multi-threaded determinism test should cover it.
  - `PhysicsMaterial` on the inner body, the child-collider warning and the checked reshape were not built by the spike.
  - Determinism rests on `CharacterID` from entities, pinned inner-body IDs and `mMaxNumHits` staying above the contact count; a scene exceeding it loses Jolt's sorting.
  - The motion tests rebuild Jolt's private `ValidateMovement` back-off; its edge cases (grazing hits, starting in contact) need their own tests.
  - MSVC and Linux were not built by the spike.
  - The generator prerequisite touches FenrirScripting core; land and test it first.
- **Engine changes land outside FenrirPhysics:** FenrirScripting (read-only fields through `Ref<T>`) and the DemoApp (`DemoCamera`, `DemoCameras`, two scenes, the F2 entry). The generator change is independently useful and can be built first.
- **Vocabulary** follows `GLOSSARY.md`: character body, carried velocity, ground state, walkable ground, steep ground, motion test, plus the foundation's and joints' terms. Avoid "character controller", "grounded", "floor" (for ground), "kinematic character", "inherited velocity" and "platform velocity" (for carried velocity). "Inner body" is private backend vocabulary, never in a public doc comment.
