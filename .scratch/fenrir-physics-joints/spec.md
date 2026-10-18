Status: resolved (built: PRs #14–#27 into dotnet-scripting)

# FenrirPhysics joints: hinges, sliders, ropes and limbs between bodies, in C++ and C#

Origin: the FenrirPhysics wayfinder map (`.scratch/fenrir-physics/map.md`), whose joints tickets (20–29) this spec collapses into one buildable plan. ADR-0013 (joints live on joint entities, with explicit local frames) records the hard-to-reverse call; ADR-0010 (the backend is hidden at compile time), ADR-0011 (physics components are plain data that physics diffs) and ADR-0012 (each event reader sees each event once) still apply. A throwaway spike (`prototype/fenrir-physics-joints-spike`) proved the model through the seam with a hinged door and a hanging chain. It builds on the foundation spec (`.scratch/fenrir-physics-foundation/spec.md`); the ragdoll spec builds on this one.

## Problem Statement

FenrirPhysics can simulate rigid bodies, but they can only touch. A game made with the engine cannot hang a door on a hinge, slide a drawer or a lift along a rail, hang a chain or a rope from the ceiling, give a weight a springy tether, or build a bridge that snaps under a load. It cannot ask how far a door is open or how hard a chain is pulling, in C++ or in C#. A later ragdoll spec needs joints to build on.

Joints bring their own traps, which other engines show:
- a joint as a component on one body allows one joint per body, which is not how the ECS works;
- frames captured from the scene when a joint is created drift, depend on timing, and shift the zero angle when the joint is rebuilt;
- scale is ignored, so anchors leave a scaled body's surface;
- jointed bodies fight their own contacts (a door against its frame, overlapping chain links);
- the backend removes a joint's bodies under it, or reorders the solve when one joint is removed.

The scripting side cannot yet write a component that names entities: C# reads `EntityLink`s but cannot make one.

## Solution

FenrirPhysics gains **joints** between two rigid bodies, or between a rigid body and the world. A joint is a `Joint` component on a **joint entity** that names `bodyA` and `bodyB`; an empty `bodyB` is the world. A body takes part in any number of joints, each on its own entity.

Game code works the ECS way:
- **Kinds:** a joint holds exactly one **joint kind**, a tagged union like `CollisionShape`: fixed, ball, distance, hinge, slider or swing-twist. Changing kind is an assignment.
- **Frames:** each joint names an explicit frame on each body, local to that body's origin and following its scale. The **rest pose** is where the two frames coincide; angles, positions, limits and targets are measured from it. `FrameFromWorld` computes both frames from one world pose when asked; physics never captures frames itself.
- **Limits, motors and springs** on the kinds that have them, every one a live field edit.
- **Breaking:** a break force and break torque. A joint over either in one step is disabled by physics and announced with a `JointBroken` event; enabling it again repairs it.
- **Joint state:** game code asks `PhysicsWorld::GetJointState(joint)` for the force and torque the joint carried last step and where its bodies sit relative to the rest pose.
- **No self-collision:** the two bodies a joint names do not collide with each other unless the joint says they may.
- **Edges:** a joint naming something that is not a valid body is an **inactive joint**: kept as written, warned about once, and active again by itself when both names are valid. Every change to a joint wakes its bodies.

Everything is available from C# through the physics scripting extension. FenrirScripting gains writable entity links so a C# component can name entities.

A new debug-line category, `Joints`, draws each joint's two frames. The DemoApp gets a scene switcher with one F-key per feature family, and two joints scenes on F4: one built in C++ (a door and a chain with every edge-case key) and one built in C# (a breakable bridge, a lift, a spring rope and a limb).

## User Stories

### Using joints from C++

1. As a game developer, I want to join two bodies by adding a `Joint` to an entity that names them, so that a joint is a component like any other.
2. As a game developer, I want a body to take part in any number of joints, so that a chain link joins both neighbours and a door can have a hinge and a latch.
3. As a game developer, I want any entity to be a joint entity, including one of the bodies it joins, so that a door can hold its own hinge.
4. As a game developer, I want to join a body to the world by leaving `bodyB` empty, so that fixing a door or a chain to the world needs no dummy entity.
5. As a game developer, I want to choose a fixed, ball, distance, hinge, slider or swing-twist joint, so that I can build doors, drawers, ropes, chains and limbs.
6. As a game developer, I want to change a joint's kind by assigning it, so that a bridge plank can go from hinged to rigid with one edit.
7. As a game developer, I want reading a joint's kind as the wrong kind to be caught, so that a type mistake fails loudly.
8. As a game developer, I want each joint's anchor on each body as an explicit frame local to that body, so that nothing depends on where things happened to be when the joint was made.
9. As a game developer, I want `FrameFromWorld` to compute both frames from one world pose, so that I can author a joint by saying where its hinge is in the world.
10. As a game developer, I want frames on a scaled body to follow its scale, so that an anchor on a crate's corner stays on the corner.
11. As a game developer, I want the joint entity's own `Transform` ignored, so that I can organise joint entities anywhere in the hierarchy.
12. As a game developer, I want angles, positions, limits and targets measured from the rest pose, so that rebuilding a joint never moves its zero.
13. As a game developer, I want limits on hinges and sliders, hard or soft, so that a door stops at its frame and a drawer at its end.
14. As a game developer, I want a velocity or position motor with a maximum force on hinges and sliders, so that I can drive a wheel, a lift or a self-closing door.
15. As a game developer, I want friction on hinges, sliders and swing-twist joints, so that a door stays where it is left.
16. As a game developer, I want a distance joint with a shortest and longest distance and a spring, so that I can make rods, ropes and springs.
17. As a game developer, I want a swing-twist joint with cone limits, a twist range and one orientation motor, so that limbs and later ragdolls can be built and driven.
18. As a game developer, I want every limit, motor, spring and friction value to be a live edit applied on the next tick, so that tuning a joint during play costs nothing.
19. As a game developer, I want SI units and radians throughout, matching `RigidBody`, so that I never guess units.
20. As a game developer, I want out-of-range values (a limit excluding the rest pose, a negative force) corrected with one warning, so that mistakes are visible but never crash.
21. As a game developer, I want to switch a joint off and on with a field, so that releasing a hanging crate is a field edit.
22. As a game developer, I want to read a joint's force, torque, relative pose and position along its free axis, so that I can show a door's opening or a chain's load.
23. As a game developer, I want a break force and break torque on a joint, so that bridges and chains snap under load.
24. As a game developer, I want a broken joint to stay on its entity, disabled, and to be repaired by enabling it, so that breaking loses no configuration.
25. As a game developer, I want a `JointBroken` event naming the joint, its bodies and the load that broke it, so that I can play a sound or score a hit.
26. As a game developer, I want jointed bodies not to collide with each other by default, with a field to let them, so that a door does not fight its frame and chain links can overlap.
27. As a game developer, I want bodies further along a chain to still collide, and a joint never to make bodies collide that the layer rule keeps apart, so that the rule is easy to predict.
28. As a game developer, I want a broken or disabled joint's bodies to collide again, so that a snapped link hits its neighbour.
29. As a game developer, I want a joint whose body is destroyed to go inactive with one warning and come back by itself when the body is valid again, so that destroying a chain link never crashes and never loses the joint.
30. As a game developer, I want a joint between bodies that cannot move to be valid but idle, so that it starts working when one becomes dynamic.
31. As a game developer, I want a body's motion type change to keep its joints, so that freezing and releasing a jointed body is a field edit.
32. As a game developer, I want a body's shape change, compound child or scale change to keep its anchors on the body, so that editing a collider never tears a joint.
33. As a game developer, I want every change to a joint to wake its bodies, so that I never call a wake function and sleep stays hidden.
34. As a game developer, I want joints to solve in the same order however they were created and removed, so that same-build determinism holds.

### Using joints from C#

35. As a script author, I want every joint type visible and writable from C#, with the C++ defaults, so that C# systems build joints exactly as C++ does.
36. As a script author, I want to name bodies by assigning an `Entity` to an `EntityLink` field, including an entity I created earlier in the same system, so that a script can build a whole jointed scene in one system.
37. As a script author, I want `joint.Kind = new HingeJoint { ... }` and `joint.Kind.Hinge.Motor.TargetVelocity = 2f`, so that kinds feel natural in C#.
38. As a script author, I want reading `Kind.Slider` on a hinge to throw, so that mistakes are caught.
39. As a script author, I want `Joint.FrameFromWorld` with a world overload, so that I author frames as C++ does and never spell the world as a magic entity.
40. As a script author, I want `PhysicsWorld.GetJointState(joint)`, so that I can read a joint's load and angle.
41. As a script author, I want `JointBroken` through an event reader field, and repair by writing `Enabled = true`, so that I handle breaks as C++ does.
42. As a script author, I want joint calls with no world, or on a non-joint, to read zeroed rather than throw, so that scripts are robust.

### Engine-wide

43. As an engine developer, I want C# to make entity links, so that every native component with an `Entity` field is writable from C#.
44. As an engine developer, I want no Jolt type reachable from the joint interface, so that the backend stays replaceable.
45. As an engine developer, I want a `Joints` debug-line category, so that any renderer can show joint frames.
46. As an engine developer, I want joint kinds the ragdoll spec can build on without adding its own, so that ragdolls are additive.

### DemoApp

47. As the engine developer, I want one F-key per feature family stepping through its scenes and back to Default, so that adding scenes does not use up keys.
48. As the engine developer, I want the F2 debug-line mode to carry across a scene switch, so that the drawn lines always match the logged mode.
49. As the engine developer, I want a C++ joints scene with a door, a chain and keys for every edge case, so that I can check joints by hand.
50. As the engine developer, I want a C# joints scene built entirely by a script, showing the four kinds the C++ scene lacks, so that I can see the C# surface working live.
51. As the engine developer, I want joints' live readings in their entity names, so that I can watch them in the Scene Hierarchy from either language.
52. As the engine developer, I want to start the DemoApp in a named scene and have it press that scene's keys, so that smoke runs cover every key.

## Implementation Decisions

### Prerequisite: writable entity links (FenrirScripting)

- `EntityLink` gains an implicit conversion from `Entity`. One new core native function fills `owner` with the active scene's entity list; `owner` is never readable from C#.
- `EntityLink.None` is the empty link (`value = 0xFFFFFFFF`, null owner). A zeroed `EntityLink` names entity 0 with no scene.
- C# can name only entities of the active scene. An entity made by `Entity.Create()` earlier in the same system works, because its value is reserved at once.
- Every later native component with an `Entity` field gets this for free.
- Rejected: physics re-linking by id (C++ readers would hold an `Entity` with a null list), and a read-only `Joint` with native setters (breaks the foundation's rule that every physics component is writable).
- The README's "Tagged unions" section says a `[TaggedUnion]` generator is planned, with its trigger: a script author wants a union, or a third engine union appears.
- These land and are tested before the joints extension work.

### FenrirPhysics: the joint component and kinds (ADR-0013)

- **`Joint`** on a joint entity. Any entity may be one, including one of its bodies. Physics never reads the joint entity's `Transform`.

  ```cpp
  struct JointFrame { Vec3 position{0}; Quat rotation{identity}; };

  struct Joint
  {
      Entity bodyA;                       // a body entity
      Entity bodyB;                       // a body entity, or empty: the world
      JointFrame frameA, frameB;          // local to each body's origin; frameB in world space for the world
      JointKind kind;
      bool enabled = true;
      bool collideConnected = false;
      float breakForce = infinity;        // N
      float breakTorque = infinity;       // N·m
  };
  ```

- **Body entities only:** `bodyA` and `bodyB` name a `RigidBody` entity or a standalone static collider's entity, never a compound child or a trigger. The world is only ever `bodyB`; physics never swaps the sides.
- **`JointKind`** is a plain-data tagged union in `CollisionShape`'s pattern: implicit conversions from each kind, `Type()` returning a `JointType` enum, and `Is<T>()`/`As<T>()` asserting the kind. Its alternatives:
  - `FixedJoint {}` and `BallJoint {}`: no settings.
  - `DistanceJoint { float minDistance = 0, maxDistance = 1; JointSpring spring; }`: keeps the two frame positions between min and max (a rod at min == max, a rope at min 0, a spring when soft). Frame rotations are ignored.
  - `HingeJoint` and `SliderJoint { JointLimits limits; JointMotor motor; float friction = 0; }`.
  - `SwingTwistJoint { float swingYLimit = π/4, swingZLimit = π/4; float twistMin = -π/4, twistMax = π/4; float friction = 0; SwingTwistMotor motor; }`.
- **Shared settings:**

  ```cpp
  struct JointSpring { float frequency = 0; float damping = 1; };   // Hz, damping ratio (1 = critical); frequency 0 = rigid
  struct JointLimits { bool enabled = false; float min = 0, max = 0; JointSpring spring; };
  enum class JointMotorMode { Off, Velocity, Position };
  struct JointMotor
  {
      JointMotorMode mode = JointMotorMode::Off;
      float targetVelocity = 0, targetPosition = 0;
      float maxForce = infinity;          // N or N·m by kind, symmetric
      JointSpring spring = {2, 1};        // how hard Position mode drives
  };
  struct SwingTwistMotor
  {
      JointMotorMode mode = JointMotorMode::Off;
      Quat targetRotation{identity};      // frameB relative to frameA; identity is the rest pose
      Vec3 targetAngularVelocity{0};      // in frameA's axes
      float maxTorque = infinity;
      JointSpring spring = {2, 1};
  };
  ```

- **`JointKind` defaults to `FixedJoint {}`,** as `CollisionShape` defaults to its first kind.
- **Axes:** each frame's local X is the hinge axis, the slider direction and the swing-twist twist axis; swing is about Y and Z. Hinge angles are signed by the right-hand rule about X.
- **Units:** SI and radians throughout, measured from the rest pose. No degree fields.
- **Limits:** `JointLimits` is on hinge and slider only; disabled means free, and its spring makes the limit soft. The rest pose lies inside the limits (min ≤ 0 ≤ max): a limit excluding 0, or with min > max, is widened to include 0 with one warning. min == max is allowed. Hinge limits lie within [-π, π]; a hinge with no limit spins freely.
- **Swing-twist limits** are hard. Swing limits are cone half-angles in [0, π] (π frees that swing); no pyramid swing. The twist range lies in [-π, π] with the rest pose inside, widened with a warning as for `JointLimits`.
- **Motors:** Off, Velocity and Position. Velocity mode reads only the velocity target, Position mode only the position (or rotation) target. A position target outside an enabled limit is clamped into it silently.
- **A hard limit always beats a motor,** whatever its `maxForce`. Physics does this privately: once a hinge or slider is at an enabled hard limit, a Velocity motor's push further out is dropped for that step (the backend solves the motor before the limit, so an infinite motor would otherwise hold a hinge about 0.2 rad past it). The same holds for `SwingTwistMotor` against the cone and twist limits. A soft limit is a spring and may be pushed past, as soft limits are.
- **Out-of-range values:** negative frequencies, damping ratios, forces, torques, frictions and break thresholds are treated as 0, and frequencies are clamped to half `Time::tickRate`, each with one warning per joint. A distance joint's negative min is raised to 0 and a max below min raised to it, each with one warning.
- **Live edits and rebuilds:** every per-kind field, `enabled`, `collideConnected` and the break thresholds are live edits applied on the next tick. Changing the kind, a body, a frame, or a body's scale recreates the backend constraint: documented as cheap but one step softer (no warm starting), not costly like shape edits.
- **Refused as backend tuning (ADR-0010):** per-joint solver step overrides, priority, the backend's draw size and user data.
- **Ragdolls** build on `HingeJoint` and `SwingTwistJoint` with Position motors and add no kinds. Anything more (position-and-velocity mode, separate swing and twist strengths, collision groups) is an additive request in that spec.
- Left addable later as new alternatives: cone, six-DOF, gear, rack-and-pinion, pulley, path.

### FenrirPhysics: frames

- Frames are local to each body entity's origin, the way game code sees the body. Positions follow the body's world scale; rotations do not.
- Physics converts them privately to the backend's centre-of-mass frames, and always builds backend constraints in local space.
- **`FrameFromWorld(bodyA, bodyB, anchor) -> JointFrames { frameA, frameB }`**, a free function computing both frames from one world pose through `GetWorldMatrix`, when called. Only an empty `bodyB` is the world; a destroyed `bodyB` is not. A body that is not alive gives a debug warning and identity frames. It needs the scene, not a physics world.
- Documented: hand-written frames on scaled or parented bodies are a trap; use `FrameFromWorld`.
- Documented: the hinge angle's sign follows which body is A, because `position` is `frameB` relative to `frameA`. Put the fixed part in `bodyA` to read a positive opening; a body hinged to the world (always `bodyB`) reads with the opposite sign. Physics never measures from "the moving end".

### FenrirPhysics: stepping order within PostTick

The foundation's `PostTick` sequence gains joint work in steps 1, 2 and a new 5b. All sequential, on the app's thread.

1. **Drain** the `Joint` watch with the body watches, using each entity's net effect:
   1. remove constraints of removed joints, and of every joint touching a body about to be destroyed or rebuilt (constraints always go before their bodies);
   2. destroy and create bodies, as in the foundation;
   3. create constraints for added joints, joints valid again, and joints of rebuilt bodies.

   Within a batch, constraints are created and removed in joint entity order. A `Joint` removed and re-added in one drain is rebuilt (its constraint removed and created again), as the foundation does for bodies; added then removed does nothing.
2. **Diff** bodies first, then joints, each in stable entity order (body shape and scale diffs decide whether a joint is notified or recreated). Each changed joint is pushed as a live update or a recreation, and wakes both its bodies.
3. Apply queued forces and impulses.
4. Step.
5. Write back dynamic bodies.
   - **5b, break overloaded joints:** for awake, enabled, active joints with a finite threshold, compute force and torque; in joint entity order, disable those over a threshold in the backend, write `enabled = false` into the component and the private copy (so it is not mistaken for a game edit), and queue `JointBroken`.
6. Sort and send contact, trigger and `JointBroken` events, once per step. `JointBroken` is sorted by joint entity.
7. Regenerate debug lines if enabled.

- **Solve order follows joint entity order, not history:** each constraint gets a private backend priority from its joint entity's order, so the solve order within an island depends only on which joints exist, not on the backend's swap-on-removal. Never exposed.
- **Inactive scenes:** nothing joint-specific. Watches keep recording; on resume step 1 applies the net effect. Nothing breaks, wakes or sends events while inactive. Destroying a world removes every constraint before any body.

### FenrirPhysics: joint state and breaking

- **`PhysicsWorld::GetJointState(Entity joint) -> JointState`**, a query reading the last completed step, like `IsSleeping` and `Bounds`. Never written into the component. Safe from parallel systems.

  ```cpp
  struct JointState
  {
      Vec3 force;          // N, world space, applied to bodyB (bodyA gets the negative)
      Vec3 torque;         // N·m, likewise
      JointFrame relative; // frameB's pose in frameA's axes; identity is the rest pose
      float position;      // hinge angle (rad, wrapped to (-π, π]), slider offset along X (m), distance length (m); 0 otherwise
  };
  ```

- Physics computes the pose readings itself from the last step's simulated poses and the frames, for every kind, never from interpolated `Transform`s or the backend's per-kind readings. A hinge angle carries no turn count. Swing-twist's `relative.rotation` is in the same terms as its motor's `targetRotation`.
- Force and torque sum every part of the joint (position, rotation, limits, motor), converted privately from the backend's per-part impulses by dividing by the step time. The Jolt backend rebuilds each kind's private axes to do this. Documented: `force` and `torque` sum the solver's impulses over the step and are approximate within a step; steady loads (a hanging chain's weight) are exact enough to test.
- **Zeroed** for a non-joint, an inactive joint and a joint not yet stepped. While asleep, force and torque keep their last awake value; while disabled they are 0. Pose readings are always current.
- **Breaking:** compared against the magnitudes of `force` and `torque`, every part included (a motor straining against a wall can break its hinge). One step over breaks it, with no averaging. Only awake, enabled joints with a finite threshold are checked.
- **A broken joint is a disabled one.** Repair is `enabled = true`, with no cooldown: a joint still overloaded breaks again on the next step with another event.
- **`JointBroken { Entity joint; Entity bodyA; Entity bodyB; Vec3 force; Vec3 torque; }`**, an engine event with the load that broke it; `bodyB` empty for the world. Sent only for threshold breaks, never for game disables. No opt-in tag. Like every event, it must be read every frame; a body removed after a break leaves the event naming a stale entity, as contact events do.

### FenrirPhysics: jointed bodies not colliding

- `collideConnected = false` by default: the two bodies a joint names do not collide unless it opts in. A live edit.
- **Only removes:** a pair collides only when the layer rule allows it and no enabled, active joint between them excludes it. `collideConnected = true` never overrides the filter.
- **Direct pairs only:** in a chain A–B–C, A and C still collide. Several joints on one pair: any one enabled joint with `collideConnected = false` excludes it.
- **Compounds and triggers:** every solid collider of both bodies is excluded. Triggers are not, even a trigger child of a jointed body.
- **Lifetime:** only while enabled and active. A disabled, broken or inactive joint excludes nothing from the next step. A joint to the world excludes nothing.
- **Queries** ignore joints entirely.
- **Contact events:** an exclusion starting between touching bodies ends their contact with the usual `ContactEnd`; one ending sends `ContactBegin` when they touch again.

### FenrirPhysics: edge cases

- **Inactive joints:** a joint is inactive when `bodyA` is empty, `bodyA == bodyB`, either name is not a body entity (no `RigidBody` and not a standalone static collider, a compound child, a trigger's entity), or either name is in another `EntityList` from the joint entity (`body != entities.HandleOf(body.GetId())`).
  - It keeps its `Joint` untouched and has no backend constraint. One warning each time it goes from valid to invalid. No event. The remaining body is woken.
  - It becomes active again by itself once both names are valid bodies (a `RigidBody` added back). A destroyed entity never comes back: its id is versioned.
  - Removing a `RigidBody` but keeping its `Collider` leaves a static body, so the joint stays active.
- **Idle joints:** both ends static or kinematic, or a static or kinematic `bodyA` joined to the world, are valid but never solved, silently. They start working when an end becomes dynamic.
- **Motion type changes** keep the constraint and log nothing. A kinematic end drags its dynamic partner and is never pushed.
- **Teleports:** writing one end's `Transform` does not move the other end; the joint pulls them back together. The teleported body's joints keep their warm starting: the backend corrects the gap by position, so last step's impulses still fit, and resetting them made the first step overshoot the load like a new joint's (5 % on a 20 kg cantilever), enough to break a joint set just over its load (found in ticket 11). Documented: move a whole jointed group in the same tick (each body's `Transform`); there is no group-teleport helper. Kinematic moves are not teleports. Reparenting a body entity is a teleport.
  - **Promised and tested:** a teleport never breaks a joint and adds no velocity to the joint's bodies. The backend corrects the gap by position, not by impulse, so neither `GetJointState` nor breaking sees it.
  - **Not promised:** how fast the bodies are pulled back together. The spike's door, teleported 0.7 m alone, returned within 10 steps without a jolt; another backend may differ.
- **Shapes and scale:** a shape or centre-of-mass change on a body that keeps its backend body (a `Collider` edit, a compound child added, removed or moved) keeps the constraint; physics tells the backend privately, so anchors stay put relative to the body's origin. A scale change recreates the constraint like a frame edit. A rebuilt body has its joints' constraints removed before it and created after it.
- **Sleep:** every diffed change to a `Joint` wakes both its bodies (the world excepted): live edits, recreation, adding or removing it, disabling, re-enabling, lowering a threshold, toggling `collideConnected`. A break needs no wake.

### FenrirPhysics: the Jolt backend (private)

All in `src/Backend/Jolt/`; no public header names Jolt.
- The backend class gains joint operations (in the spike: `CreateJoint`, `DestroyJoint`, `UpdateJoint`, and reading load; the spike's `ResetWarmStart` was dropped in ticket 11). Physics keeps a private index from each body to its joints.
- Kinds map to Jolt's fixed, point, distance, hinge, slider and swing-twist constraints, built in local space against centre-of-mass frames; the world end is `Body::sFixedToWorld`.
- min == max limits, the rest-pose widening and clamping are mapped privately.
- Shape changes call `NotifyShapeChanged`; every update calls `ActivateConstraint` (no Jolt edit wakes anything).
- **No-collide:** one custom Jolt `GroupFilter` over a pair → count table of excluding enabled joints. Only bodies in at least one excluding joint get a `CollisionGroup` (its group ID the body's index). The table is patched on the main thread in `PostTick` with the joint diff, before the step, so it is read-only while job threads call the filter. Room for general collision groups as one more test inside it. Rejected: `GroupFilterTable` (cannot express a body jointed to two neighbours) and rejecting in `OnContactValidate`.
- **Priority:** each constraint's Jolt priority comes from its joint entity's order.
- **Load:** per-part lambdas divided by the step time; hinge and swing-twist torque rebuilt from the axes the constraint parts derive, from the poses after the step. Each kind needs its own conversion.

### FenrirPhysics: debug lines

- `DebugDraw::Joints`, a new category included in `All`, with a C# mirror. For each joint it draws both frames as axes: `frameA` long (red, green, blue), `frameB` short (pink, yellow, cyan). They coincide at the rest pose and while the joint holds.
- A disabled (broken) joint's axes are dimmed. An inactive joint draws only the frames on its surviving bodies, in grey.
- No limit arcs or cones yet.

### FenrirPhysicsScripting and Fenrir.Physics

- **`Fenrir.Physics` contract:**
  - a new `Joints.cs`: `[NativeComponent("Fenrir.Physics.Joint")] public struct Joint : IComponent` with `EntityLink BodyA, BodyB` (both `EntityLink.None` from `new`), `JointFrame FrameA, FrameB`, `JointKind Kind = new()`, `bool Enabled = true`, `bool CollideConnected = false`, `float BreakForce, BreakTorque = float.PositiveInfinity`; the kinds and settings (`FixedJoint`, `BallJoint`, `DistanceJoint`, `HingeJoint`, `SliderJoint`, `SwingTwistJoint`, `JointSpring`, `JointLimits`, `JointMotor`, `SwingTwistMotor`, `JointMotorMode`, `JointType`, `JointFrame`, `JointFrames`, `JointState`) with the C++ names in PascalCase and the C++ defaults; and `Joint.FrameFromWorld`;
  - `JointBroken` in `Events.cs`: `EntityLink Joint, BodyA, BodyB`, `Vector3 Force, Torque`; `BodyB.IsNull` means the world; its doc comment says to read it every frame;
  - `JointState PhysicsWorld.GetJointState(Entity joint)` on `PhysicsWorld`, by value, no `bool`/`out`;
  - two new `PhysicsNative` entries.
- **`JointKind` in C#** is hand-written in `CollisionShape`'s pattern: an explicit-layout struct with a private one-byte tag and each kind at the union's offset; `Type`; one checked `[UnscopedRef] ref` view per kind named after it (`Fixed`, `Ball`, `Distance`, `Hinge`, `Slider`, `SwingTwist`), even the empty ones; implicit conversions from each kind; `InvalidOperationException` on the wrong view. This is the surface a `[TaggedUnion]` generator would emit, so it can replace both unions without touching a caller. `new JointKind()` is C++'s default kind.
- `JointKind` never appears in an event or a C++-to-C# call argument (the README's bool caution is about by-value copies from C++).
- `default(Joint)` names entity 0 with no scene, so it reads inactive with a warning. It does not crash.
- **`Joint.FrameFromWorld`** is a native function, so the exact world matrix and the empty-is-world rule live once in C++: `FrameFromWorld(Entity bodyA, Entity bodyB, JointFrame anchor)` and `FrameFromWorld(Entity bodyA, JointFrame anchor)` for the world. Both return `JointFrames`. A body not alive gives a debug warning and identity frames.
- **Robustness:** `GetJointState` with no world, or on a non-joint, returns zeroed with the foundation's debug warning once per native site. An inactive joint returns zeroed with no extra warning; a joint created this tick reads zeroed until its first step. These go in `PhysicsWorld`'s remarks. C# never needs a wake call.
- **`FenrirPhysicsScripting`:** the mappings through stand-ins (an `EntityLinkView` where each `Entity` sits, and a plain mirror of `JointKind`'s private union, as for `CollisionShape`), the `JointBroken` registration and the two native functions. FenrirPhysics itself still names nothing from Iris.

### DemoApp: scene switching

- **`DemoScenes`** (`examples/DemoApp/src/DemoScenes.hpp`), a DemoApp switcher, not engine code, replacing `SwitchOnF3`. Each scene registers its name, its family key, a one-line key help logged on entry, and optionally a key sequence for smoke runs.
- **One F-key per feature family,** stepping through that family's scenes and back to Default. F3: Default → Physics (C#) → Default. F4: Default → Joints (C++) → Joints (C#) → Default. F5–F8 are left for the character controller, vehicles, ragdolls and soft bodies. Another family's key jumps to its first scene. A key steps only through scenes that registered for it: without scripting F4 toggles Default ⇄ Joints (C++) and F3 does nothing.
- **The F2 mode becomes app-wide** and is re-applied to each newly active world, fixing today's desync between the logged mode and the drawn lines. The F2 cycle gains "joints" before "all".
- **`FENRIR_DEMO_SCENE=<name>`** starts in that scene. With **`FENRIR_DEMO_KEYS=1`** the switcher presses the active scene's registered keys as real `KeyboardKeyEvent`s, one a second after two seconds of settling, so C++ and C# readers both see them.
- Each scene reads its keys in every scene and acts only while active.
- **`CSharpScene { int Scene; int Built; }`** replaces `CSharpPhysicsScene`, registered once in the demo's scripting extension. `Playground.cs` and the new `scripts/Physics/Joints.cs` each build where they find their own id unbuilt.
- **Readings go into joint entity names** every frame (for example "Door hinge: 0.73 rad, 12 N"), in both languages; key 0 logs every joint's full state. No new overlay.

### DemoApp: Joints (C++)

The spike's scene, rewritten against the final API.
- **Keeps:** the door (a hinge with limit, friction and motor, on a static post scaled (0.2, 2.2, 0.2) under a "Door frame" entity, the post as `bodyA`), the eight-link ball chain from a world point, and the edge-case keys.
- **Drops:** the spare joint and its P key, the scene's own frame drawing, and its `FrameFromWorld` bug.
- **Adds:** a trigger child on the door overlapping its post, logging `TriggerEnter` with the post.
- **Keys:** 1 push the door; 2 cycle the door motor (off, +velocity, −velocity, position 0), logging whether it was asleep; 3 destroy link 3; 4 teleport the door alone; 5 teleport the door and its frame; 6 scale link 5; 7 add or remove a lump on link 6; 8 reshape link 6; 9 toggle `collideConnected` on the chain; 0 log every joint's state; B toggle a 15 N `breakForce` on chain joint 1; R repair disabled joints; L toggle the door's limits.

### DemoApp: Joints (C#)

C++ places only the marker. `scripts/Physics/Joints.cs` builds everything in one system, linking to entities it created in that system, with frames from `Joint.FrameFromWorld`.
- **Breakable bridge:** planks hinged end to end between two static posts; the middle hinge has a `breakForce`, read through `EventReader<JointBroken>` and logged.
- **Kind swap:** the middle joint swaps between hinge and fixed by assigning `Kind`.
- **Motorised lift:** a slider with limits, its motor driven through `Kind.Slider`.
- **Spring rope:** a distance joint with a spring holding a bobbing weight from a world point, frames from the world overload.
- **Limb:** a swing-twist arm with cone limits and an orientation motor.
- **Keys** (same key for the same job as the C++ scene, avoiding WASD, the F-keys and J): B drop a weight on the bridge (made at runtime with `Entity.Create`); R repair; K swap the middle joint; 1 cycle the lift motor (off, up, down, position); 2 toggle the limb motor between rest and a raised pose; 0 log every joint's state.
- No edge-case keys; C# reaches them unchanged.

## Testing Decisions

- **What a good test is:** as in the foundation. It drives the engine through its public interface and asserts on component values, world transforms, joint state, events read and debug lines. Never on private state or which backend calls were made. Tests run against real Jolt. Same-build determinism lets tests assert exact results; joint drift and resting positions use a tolerance (chains drift 1–2 cm while swinging, under 1 mm settled).
- **Seam 1, the App stepped one frame at a time with a chosen delta** (`FenrirPhysicsTests`):
  - each kind holds its bodies at the rest pose, and its free axis moves; frames on scaled and rotated parents; `FrameFromWorld` round trips, its world rule and its dead-body warning;
  - limits (hard, soft, widened with a warning), motors in both modes, an infinite Velocity motor held at a hard hinge, slider and swing-twist limit, friction, distance min and max, swing-twist cones and twist, out-of-range warnings;
  - live edits reach physics on the next tick; kind, body, frame and scale changes recreate without moving the rest pose; a `Joint` removed and re-added in one tick is rebuilt; the defaults of every kind and setting;
  - `GetJointState`: the chain's top joint reads its weight (49.0 N for five 1 kg links), hinge angle and sign, zeroed for non-joints, inactive and unstepped joints, kept while asleep, 0 while disabled;
  - breaking: one step over breaks with one `JointBroken` sorted by joint; repair while overloaded breaks again; infinite never breaks; game disables send nothing;
  - no-collide: default exclusion, `collideConnected`, chains two apart colliding, the layer rule winning, triggers not excluded, exclusion ending on disable or break, contact events on a toggle, queries unaffected;
  - edges: each inactive case with one warning per transition and reactivation; idle joints starting when an end becomes dynamic; motion-type switches; destroying a body mid-swing (no crash, constraints before bodies); teleports, a teleport never breaking a joint (even with a low threshold) nor adding velocity; shape changes keeping anchors; every edit waking a sleeping pair; inactive scenes;
  - solve order: removing an earlier joint leaves a chain bit-identical to disabling it; bit-identical runs, single- and multi-threaded;
  - `Joints` debug lines: counts for known joints, dimmed and grey cases.
  Prior art: the foundation's `PhysicsTestApp` and `PhysicsProbe`, and the spike's `[joints-spike]` cases on the prototype branch.
- **Seam 2, `EntityList`'s public interface:** nothing new is needed in `FenrirECSTests`; the joint watch uses the foundation's component watches. The other-scene check rests on `Entity`'s `==` comparing lists; add an `EntityList` test pinning that if none exists. Prior art: `FenrirECSTests`.
- **Seam 3, the scripting fixture harness:**
  - in `ScriptingTests`: `EntityLink` from an `Entity` fills the owner, `EntityLink.None` is empty, a link to an entity created earlier in the same system reads back, and a native component's `Entity` field written from C# reads correctly in C++;
  - in `PhysicsScriptingTests` with a `Joints.cs` fixture: `Joint`, `JointState` and `JointBroken` validate against their native layouts; `new Joint()` equals `Joint{}` (empty links, infinities, kind defaults); a C# system creates two bodies and a joint in one system and a hinge holds after playback; assigning a kind and editing a motor target through the view reach physics; the wrong view throws; `GetJointState` reads a hinge angle and reads zeroed with no world or for a non-joint; `FrameFromWorld` matches C++ under a scaled, rotated parent and the world overload equals C++ with an empty `bodyB`; `EventReader<JointBroken>` sees one break and `Enabled = true` repairs.
  Prior art: the foundation's `PhysicsScriptingTests` and its fixture, `ScriptingProbe`, `HeadlessApp`.
- **The DemoApp scenes are verified by hand,** as the spikes were:
  - **Switching:** F4 steps Default → Joints (C++) → Joints (C#) → Default, logging key help; F3 still works; the F2 mode carries across; a key in one scene does nothing in another; `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs each joints scene through its keys with no errors, in Debug and Release.
  - **Joints (C++):** the door swings, stops at its limits and shows its angle; with limits on, a velocity motor holds it at its limit; a motor edit wakes a sleeping door; the trigger child reports its post; the chain hangs with frames together and shows its load, links two apart collide; 9 pushes overlapping links apart; 3 gives one warning per joint, the links below fall and inactive joints draw grey; 4 pulls the door back without a jolt, 5 lines up at once; 6, 7, 8 keep anchors put; B then a swing breaks joint 1 with one event and it draws dimmed, R repairs it.
  - **Joints (C#):** builds with no warning and no inactive joint; B breaks the bridge with one logged event and R repairs; K visibly stiffens the bridge; 1 drives the lift between its limits and to a position; the rope bobs; the limb stays in its cone and 2 raises it; names show readings live.
  - **With `Joints` lines on,** every joint's frames coincide at rest.

## Out of Scope

- Cone, six-DOF, gear, rack-and-pinion, pulley and path joints (addable later as `JointKind` alternatives; the last four need concepts the model lacks).
- Ragdoll skeletons and pose driving, vehicles, character bodies and soft bodies: each its own spec.
- General collision groups (any set of bodies ignoring each other), including excluding a whole chain: with ragdolls and character bodies.
- Articulations (reduced-coordinate joints): Jolt has none.
- Per-joint solver settings and priority (refused, ADR-0010), and a world-level solver setting (not needed: chain drift is acceptable).
- Averaging loads over steps for breaking, and break cooldowns.
- A group-teleport helper.
- Limit arcs and cones in debug lines; configurable debug colours.
- A position-and-velocity motor mode, stiffness springs, soft swing-twist limits.
- A `[TaggedUnion]` source generator (wanted; trigger: a script author wants a union, or a third engine union appears).
- A separate demo app per area (trigger: the DemoApp runs out of family keys F3–F12, or its start-up time or clutter gets in the way of hand checks).
- An on-screen text overlay in the DemoApp.

## Further Notes

- **Decided in this spec** (raised by the spike or left unstated by the tickets, answered by the user when the spec was reviewed, 2026-10-17):
  1. A hard limit always beats a motor, privately, rather than a finite default `maxForce` or documenting that an infinite motor overrides limits. It keeps the rest-pose-inside-limits promise and matches ticket 23's clamping of position targets.
  2. `force` and `torque` are documented as approximate within a step; tests pin steady loads only.
  3. A teleport never breaks a joint and adds no velocity (promised, tested); the pull-back speed is not promised. This replaces ticket 26's expectation that a lone teleport is violent, which the spike disproved.
  4. The hinge angle's sign follows which body is A, documented, rather than measured from the moving end.
  5. A `Joint` removed and re-added in one drain is rebuilt, as bodies are; the spike kept it.
  6. The defaults the tickets left unstated, in the shared-settings shapes above: `JointSpring {0, 1}`, `JointLimits {0, 0}`, zero motor targets, `SwingTwistMotor` matching `JointMotor`, `JointKind` defaulting to `FixedJoint`, identity `JointFrame`.
- **Decision history:**
  - the wayfinder map `.scratch/fenrir-physics/map.md` and its joints tickets 20–29;
  - ADR-0013, with ADR-0010, ADR-0011 and ADR-0012 still applying;
  - research branches `research/jolt-constraints` and `research/engine-joint-models`;
  - the spike branch `prototype/fenrir-physics-joints-spike` (`ee05236`).
- **Where tickets disagreed,** the later one is followed: the query is `GetJointState` in both languages (28, over 24's `JointState`); `FrameFromWorld` with a dead body warns and returns identity frames in C++ too (28); a rebuild is cheap, not costly (23 and 27).
- **Risks:**
  - Load conversion is per kind and backend-specific: slider, distance and swing-twist torque conversions were not built by the spike, which did hinge and ball only. Test each kind's steady load early.
  - The no-collide `GroupFilter` (the spike's `JointPairs`) runs on job threads; its pair table must stay read-only during the step, which the multi-threaded determinism test should cover.
  - Chains stretch 1–2 cm while swinging; if ragdolls prove worse, a later ticket may propose a neutral world-level setting, never a per-joint one.
  - MSVC and Linux were not built by the spike.
  - Writable entity links touch FenrirScripting core and every native component with an `Entity` field; land and test them first.
- **Engine changes land outside FenrirPhysics:** FenrirScripting (writable entity links, README) and the DemoApp (scene switcher, markers, two scenes). The entity-link change is independently useful and can be built first.
- **Vocabulary** follows `GLOSSARY.md`: joint, joint entity, joint kind, rest pose, breaking (a joint), joint state, inactive joint, entity link, plus the foundation's terms. Avoid "constraint" outside the backend.
