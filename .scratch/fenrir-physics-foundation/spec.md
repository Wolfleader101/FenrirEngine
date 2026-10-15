Status: ready-for-agent

# FenrirPhysics foundation: rigid-body physics over Jolt, behind a seam, wired into the engine

Origin: the FenrirPhysics wayfinder map (`.scratch/fenrir-physics/map.md`), whose decision tickets this spec collapses into one buildable plan. ADR-0010 (the backend is hidden at compile time), ADR-0011 (physics components are plain data that physics diffs) and ADR-0012 (each event reader sees each event once) record the hard-to-reverse calls. A throwaway spike (`prototype/fenrir-physics-spike`) proved the seam, the ECS model and the stepping end to end. Later specs (joints and constraints, character controller, vehicles, ragdolls, soft bodies) build on this one.

## Problem Statement

FenrirEngine has no physics. `packages/FenrirPhysics` is an empty folder. A game made with the engine cannot make objects fall, collide, rest on the ground, be pushed or detect that something entered an area. It cannot ask "what does this ray hit?" either, in C++ or in C#.

The engine developer wants a proper physics package that is the engine's own:
- its own components and systems, following the engine's ECS rules;
- a public interface that never names the library underneath, so JoltPhysics, used today, can be swapped later for Rapier, Box3d or an in-house engine without anyone's code changing;
- optional, like every engine package, so an app that does not want physics does not pay for it.

Several things in the engine also stand in the way, and physics needs them fixed first:
- there is no correct world transform: the DemoApp renderer composes only one parent level, without rotating the child's offset;
- the ECS cannot report that a component was added or removed;
- event readers see each event twice per frame, and none (or several times) in the fixed-step phase;
- scenes move in memory when a new one is created, leaving `Entity` handles dangling;
- destroying a scene can silently change which scene is active;
- the fixed-step loop has a single phase and no cap on how many steps a slow frame can owe;
- tests cannot drive the app deterministically, because its frame time comes from the wall clock.

## Solution

A new **FenrirPhysics** package: 3D rigid-body physics with Jolt as its **physics backend**. Jolt is hidden at compile time: only one private directory of FenrirPhysics includes it, and no public header names it.

Game code works the ECS way:
- **Bodies:** add a `RigidBody` (static, kinematic or dynamic) and a `Collider` holding a **collision shape** (box, sphere, capsule, convex hull or triangle mesh) to an entity.
- **Compound bodies:** put colliders on child entities.
- **Optional data:** add `PhysicsMaterial`, `CollisionFilter` (layer and mask), `Trigger` or `ContactEvents`.
- **Simulation:** each scene that uses physics gets its own **physics world**, which steps once per fixed tick. It writes moving bodies back to their `Transform`s, and between ticks it blends their poses so motion looks smooth at any frame rate.
- **Asking questions:** game code reads contacts and trigger overlaps as engine events, asks the world raycast, shape-cast and overlap questions, applies forces, and can switch on debug lines that any renderer can draw.

Everything is available from C# through a physics scripting extension.

The engine gains what physics needs, each as a neutral capability any package can use:
- a deterministic "run one frame with this delta" entry point on the App;
- `BeforeTicks`, `PreTick`, `PostTick` and `AfterTicks` phases, and a cap on ticks per frame;
- scene resources;
- stable scene storage and a correct `DestroyScene`;
- component watches;
- world-transform functions;
- per-reader event cursors.

The DemoApp gets a physics scene (static ground, falling boxes, a kinematic paddle), and its renderer and editor move onto correct world transforms.

## User Stories

### Using physics from C++

1. As a game developer, I want to install physics with one call that returns an owner, so that turning physics on is as simple as turning scripting on.
2. As a game developer, I want to give an entity a `RigidBody` and a `Collider`, so that it becomes a simulated body without my touching any physics library.
3. As a game developer, I want an entity with a `Collider` and no `RigidBody` (on itself or above it) to be static, so that level geometry needs no extra component.
4. As a game developer, I want to choose a body's motion type (static, kinematic, dynamic) as a field, so that freezing a ragdoll or releasing a platform is a field edit, not a structural change.
5. As a game developer, I want to set mass, damping and gravity scale on a `RigidBody`, so that bodies feel right.
6. As a game developer, I want to read and write a dynamic body's linear and angular velocity as fields, so that I can launch or stop a body directly.
7. As a game developer, I want dynamic bodies to fall under gravity and come to rest on static geometry, so that the world behaves physically.
8. As a game developer, I want a kinematic body that I move by editing its `Transform` in `Tick` to push dynamic bodies without being pushed, so that moving platforms and paddles work.
9. As a game developer, I want writing a dynamic body's `Transform` to teleport it, so that respawning or placing an object needs no special API.
10. As a game developer, I want to set a collider's shape by value (`collider.shape = SphereShape{1.0f}`), so that switching a box to a sphere is one assignment.
11. As a game developer, I want to edit a shape's dimensions in place (`collider.shape.As<BoxShape>().halfExtents.y = 2`), so that resizing needs no physics call.
12. As a game developer, I want reading a shape as the wrong kind to be caught, so that a type mistake fails loudly instead of corrupting data.
13. As a game developer, I want convex-hull and triangle-mesh colliders built from vertex and index data I supply, so that physics never depends on the renderer or an asset format.
14. As a game developer, I want identical shapes to share one backend shape automatically, so that a thousand crates do not cost a thousand shapes.
15. As a game developer, I want colliders on child entities to join their nearest ancestor body as a compound body, so that complex shapes are built from the hierarchy.
16. As a game developer, I want a dynamic body under a parent to simulate independently in world space, with its local `Transform` written back correctly, so that parenting for organisation does not break physics.
17. As a game developer, I want the world scale of an entity applied to its collider, so that scaling a crate scales its collision.
18. As a game developer, I want friction and restitution on a `PhysicsMaterial`, so that ice is slippery and rubber bounces.
19. As a game developer, I want Godot-style collision layers and masks on a `CollisionFilter`, so that I decide what collides with what.
20. As a game developer, I want to name my 32 layers and give each a default mask in the physics config, so that layer code reads like "Player" and "Enemy" and logs show names.
21. As a game developer, I want a `Trigger` collider that reports overlaps without pushing anything, so that pickups and zones work.
22. As a game developer, I want a trigger that is a child of a moving body to move with it and keep its own filter, so that a pickup radius can follow the player.
23. As a game developer, I want to switch a trigger, or a body's contact reporting, off and on by a field, so that toggling does not need a structural change.
24. As a game developer, I want `ContactBegin` and `ContactEnd` events for bodies that ask for them, so that I can play sounds or apply damage on impact.
25. As a game developer, I want `TriggerEnter` and `TriggerExit` events, so that I know when something enters or leaves a zone.
26. As a game developer, I want contact events to name both bodies and both colliders, with a point, normal and closing speed, so that I can tell what hit what and how hard.
27. As a game developer, I want a body falling asleep not to end its contacts or leave its triggers, so that resting objects do not fire spurious events.
28. As a game developer, I want every begin to have a matching end, even when a body is destroyed or its component removed, so that my bookkeeping never leaks.
29. As a game developer, I want to ask what a body is touching right now, so that I do not need "stay" events to know a character is grounded.
30. As a game developer, I want raycasts (closest and all hits), so that I can implement shooting and line of sight.
31. As a game developer, I want shape casts with the same shape values colliders use, so that I can sweep a sphere or capsule through the world.
32. As a game developer, I want overlap and point-overlap queries, so that I can find what is inside an area.
33. As a game developer, I want query results to name entities and to fill a buffer I supply, so that queries allocate nothing and fit the ECS.
34. As a game developer, I want queries filtered by mask, one ignored body and whether to hit triggers, so that a gun does not hit its owner or a pickup radius.
35. As a game developer, I want to apply forces, impulses and torques to a body by entity, so that I can push things without touching the backend.
36. As a game developer, I want to read whether a body is sleeping and its bounds, so that gameplay and tests can inspect physics state.
37. As a game developer, I want to set gravity per world at any time, so that a level can have low gravity.
38. As a game developer, I want each scene to keep its own physics world, paused while the scene is inactive, so that switching scenes and back resumes exactly where it was.
39. As a game developer, I want bodies to move smoothly on a 144 Hz display with a 60 Hz tick, so that the game does not stutter.
40. As a game developer, I want to opt a body out of interpolation, so that bodies I teleport every frame do not lag a step.
41. As a game developer, I want the same inputs to give the same simulation on the same machine and build, so that replays and tests are reproducible.
42. As a game developer, I want switchable debug lines (colliders, contacts, velocities, bounds) that any renderer can draw, so that I can see what physics sees.
43. As a game developer, I want physics to be optional at build time, so that an app without physics does not build Jolt.

### Using physics from C#

44. As a script author, I want every physics component visible and writable from C#, so that C# systems configure bodies exactly as C++ does.
45. As a script author, I want `collider.Shape = new BoxShape(...)` and `collider.Shape.Box.HalfExtents.Y = 2f`, so that shapes feel natural in C#.
46. As a script author, I want reading `Shape.Sphere` on a box to throw, so that mistakes are caught.
47. As a script author, I want physics native functions for forces, queries, contacts, sleep, bounds, gravity, debug draw and collision meshes, so that C# can do everything C++ can.
48. As a script author, I want contact and trigger events readable through an event reader field, so that I handle each event once.
49. As a script author, I want physics calls on an entity without a body, or in a scene without physics, to do nothing rather than throw, so that scripts are robust.
50. As a script author, I want to read and set an entity's world transform, so that I can place children in world space.

### Engine-wide

51. As an engine developer, I want each event reader to see each event once, in C++ and C#, so that discrete events (key presses, contacts) are never handled twice.
52. As an engine developer, I want readers in the fixed-step phases never to miss an event at high frame rates, so that input read in `Tick` is reliable.
53. As an engine developer, I want events sent by systems running in parallel to arrive in a deterministic order, so that determinism holds with parallel systems.
54. As an engine developer, I want `PreTick` and `PostTick` around `Tick`, and `BeforeTicks` and `AfterTicks` around the whole fixed loop, so that packages can hook before and after the simulation without ordering tricks.
55. As an engine developer, I want a cap on ticks per frame, so that a long frame slows the game briefly instead of spiralling.
56. As an engine developer, I want one correct world transform (an exact matrix and a decomposed transform) and a way to set it, so that rendering, physics and gameplay agree on where things are.
57. As an engine developer, I want to watch a component type for adds and removes, so that a package can react to them in a batch, in order.
58. As an engine developer, I want packages to keep per-scene state in the scene, so that it lives and dies with the scene.
59. As an engine developer, I want creating a scene not to invalidate existing entities, so that multi-scene apps are safe.
60. As an engine developer, I want destroying a scene never to change which scene is active silently, so that scene management is predictable.
61. As an engine developer, I want to run the app one frame at a time with a chosen delta, so that tests of time-dependent behaviour are deterministic.
62. As an engine developer, I want Jolt's build settings kept from leaking into the engine (runtime library, LTO, warnings), so that adding physics does not break the rest of the build.
63. As an engine maintainer, I want no Jolt type or include path reachable outside FenrirPhysics, so that replacing the backend means rewriting one directory.

### DemoApp

64. As the engine developer, I want a DemoApp physics scene with a ground, falling coloured boxes and a kinematic paddle, so that I can see physics working.
65. As the engine developer, I want a key that cycles the physics debug lines in the DemoApp, so that I can see colliders and contacts.
66. As the engine developer, I want the DemoApp renderer to draw children at their correct world transform, so that hierarchies render correctly.
67. As the engine developer, I want the editor gizmo to work in world space, so that dragging a child moves it where I drag it.

## Implementation Decisions

### The test seam: running the App one frame at a time

- `App` gains a public way to run one frame as if a given delta had passed (for example `RunFrame(double deltaTime)`), running the init phases on the first call. `App::Run` becomes a loop over it with the wall-clock delta. This is the one new seam every engine-level test goes through; the scripting tests' headless app moves onto it.

### FenrirApp, FenrirScheduler, FenrirTime

- **Phases.** `SchedulePriority` gains `BeforeTicks`, `PreTick`, `PostTick` and `AfterTicks`. A frame runs: `PreUpdate`, `BeforeTicks` (once), then `PreTick`, `Tick`, `PostTick` once per fixed step (zero or more times), then `AfterTicks` (once, even with no tick), `Update`, `PostUpdate`, then the event swap. Generic phases, not scheduler before/after ordering.
- **Tick cap.** `Time` gains `maxTicksPerFrame` (default 5). Time owed beyond it is dropped from the accumulator with a warning.
- **Stable scene storage.** Scenes live at stable addresses (for example one heap allocation each), so creating a scene never moves another, and `Entity` handles and scene resources stay valid.
- **`DestroyScene`** keeps the active scene correct when an earlier scene is destroyed. Destroying the active scene is refused, or defined explicitly.
- **`ThreadPool`** exposes its thread count (Jolt's job system needs it).
- **Events (ADR-0012):**
  - Each queue counts the events sent to it. An `EventReader<T>` remembers the count it last read up to; a read yields only newer events as up to two ranges, never a copy into shared state, and reports how many it missed if it went two swaps without reading.
  - A system owns its readers (a member, or captured in its lambda).
  - `App::ReadEvents<T>()` is removed. The DemoApp's `CameraController`, `Editor` and `Window`, the scripting bindings and `LogBursts` migrate; `LogBursts`' hand-rolled count goes.
  - The swap at the end of a frame happens only if at least one tick ran since the last swap.
  - Sends from systems the scheduler runs in parallel go into a per-system buffer, appended to the queue after the batch in registration order. Sequential systems send directly. Queue lookup and creation are guarded.
  - Payloads stay plain data; anything richer is referenced by entity or handle.

### FenrirScene

- **Scene resources:** a typed per-scene slot (`GetResource<T>`, `AddResource<T>`, `RemoveResource<T>`), one value per type, destroyed with the scene. A physics world is one.

### FenrirECS

- **Component watches.** `EntityList::Watch(ComponentType)` returns a move-only `ComponentWatch`. While it exists it records every add and remove of that type, in the order they happened, as `{ entity, Added | Removed }`; draining hands back the record and empties it.
  - Built privately on EnTT's construct and destroy signals; no EnTT type in the interface (ADR-0008).
  - Dynamic (C#-defined) storages gain the same recording, so a watch never silently misses a C# component.
  - Destroying an entity or clearing the list reports a removal for each watched component.
  - Several independent watches per type are allowed. A watch belongs to one entity list. It records only changes after it is created.
  - Edits are not reported (ADR-0011).
- **World transform.** Free functions computed on demand by walking `Relationship` (no cached component; a private cache may be added later behind them):
  - `GetWorldMatrix(entity)`: the exact product of local matrices, shear included.
  - `GetWorldTransform(entity)`: position, rotation and scale composed as `pos = parentPos + parentRot·(parentScale ⊙ localPos)`, `rot = parentRot·localRot`, `scale = parentScale ⊙ localScale` (component-wise, so lossy under rotation).
  - `SetWorldTransform(entity, world)`: the inverse, `local.pos = parentRot⁻¹·(world.pos − parentPos) ⊘ parentScale`, `local.rot = parentRot⁻¹·world.rot`, `local.scale = world.scale ⊘ parentScale`. With no parent it sets `Transform`.
  - Documented: the matrix and the decomposed transform differ only under shear (non-uniform parent scale with a rotated child).

### FenrirPhysics: the seam (ADR-0010)

- No public header names or includes Jolt; Jolt is linked `PRIVATE`.
- A private, uninstalled header declares a concrete backend class (no virtual calls). Its only implementation lives in a Jolt directory, the only code that includes Jolt. The rest of FenrirPhysics (sync, events, queries, debug lines) calls the backend class.
- **Public identity is entities and components.** No public body handle; FenrirPhysics maps entities to backend ids privately, and Jolt's per-body user data holds the entity.
- **Refused:**
  - backend tuning settings: public settings are gravity, step behaviour and capacities as plain limits;
  - native escape hatches: no backend type is reachable.
- **Accepted:** behaviour differences between backends. FenrirPhysics promises behaviour only where its tests pin it.
- **Public math** is `Fenrir::Math`; conversion is private.

### FenrirPhysics: components (ADR-0011)

- **`RigidBody`:** `MotionType { Static, Kinematic, Dynamic }`, mass, damping, gravity scale, linear and angular velocity (read/write), and `bool interpolate = true`.
- **`Collider`:** holds a `CollisionShape` by value. `CollisionShape` is a plain-data tagged union of `BoxShape { halfExtents }`, `SphereShape { radius }`, `CapsuleShape { halfHeight, radius }`, `ConvexHullShape { hull }`, `MeshShape { mesh }`; hull and mesh hold a FenrirPhysics-owned collision-mesh handle. It has implicit conversions from each kind, a `Type()`, and a checked view (`Is<T>()`, `As<T>()`, which asserts the kind). Shape of the C++ type, from the spike (trimmed):

  ```cpp
  class CollisionShape
  {
    public:
      CollisionShape(BoxShape box);       // implicit, one per kind
      CollisionShape(SphereShape sphere);
      ShapeType Type() const;
      template <typename T> bool Is() const;
      template <typename T> T& As();      // asserts Is<T>()
    private:
      ShapeType m_type;
      union { BoxShape m_box; SphereShape m_sphere; /* capsule, hull, mesh */ };
  };
  struct Collider { CollisionShape shape; };
  ```

- **`PhysicsMaterial`** (friction, restitution) and **`CollisionFilter`** (`std::uint32_t layers`, `std::uint32_t mask`): optional, with defaults.
- **`Trigger`** and **`ContactEvents`:** components with `bool enabled = true`. The field is there because the ECS cannot expose an empty type to C#, and it makes toggling a field edit.
- **Static bodies:** a collider with no `RigidBody` on its entity or any ancestor is static; `RigidBody { Static }` means the same.
- **Compound bodies:** one collider per entity. A collider on an entity without its own `RigidBody` belongs to the nearest ancestor that has one.
- **Collision meshes:** `CreateCollisionMesh(vertices, indices)` cooks once and returns a handle, shared by colliders; released explicitly. Loading from a model or file is the caller's job.
- **Shape sharing:** identical primitive shapes share one backend shape through a private cache keyed by value.

### FenrirPhysics: install, worlds, settings

- **`Physics::Install(App&, PhysicsConfig)`** returns a move-only owner of the backend's process-wide state (allocator, factory, type registration, logging into the app's logger), the installed systems and the default settings.
  - Destroying it first removes every scene's physics world, then tears down the backend.
  - The owner must outlive `App::Run`: the installed systems refer to it.
- **`PhysicsConfig`:** default gravity, capacities (max bodies, body pairs, contacts), named collision layers with default masks, worker-thread use (or single-threaded stepping), and app-wide interpolation on or off.
- **One physics world per scene,** kept as a scene resource.
  - Created the first time its scene runs with any physics component; on creation it scans the colliders already there (a watch records only later changes).
  - Not stepped while its scene is inactive; destroyed with its scene.
  - Reached by `PhysicsWorld::Of(scene)`.
- **Per-world settings:** gravity can change at any time. Capacities are fixed once a world exists.

### FenrirPhysics: stepping

One step per tick at `Time::tickRate`, with `ceil(tickRate × 60)` collision steps. Only the active scene's world steps.

**`BeforeTicks`** (once per frame): for each interpolated body, restore its true pose. If its `Transform` no longer equals the interpolated pose physics wrote, game code changed it during `Update`: keep the game's value (it becomes a teleport).

**`PostTick`** (per step), all sequential, on the thread that runs the app:
1. Drain the component watches; create and destroy bodies in one batch using each entity's net effect (added-then-removed: nothing; removed-then-added: rebuild).
2. Diff components and world transforms against physics's private copies, in a stable entity order, and push the differences:
   - a teleport for a static or dynamic body whose pose changed;
   - `MoveKinematic` for every kinematic body, every step, even if unmoved, or it keeps its last velocity;
   - velocity changes, shapes and scale (rebuilding the backend shape), filters, motion types.
3. Apply queued forces and impulses.
4. Step.
5. Write back dynamic bodies' poses through `SetWorldTransform` and their velocities into `RigidBody`. Update the private copy by reading the pose back through `GetWorldTransform`, not from the backend's result, so rounding through a parent is not mistaken for a game edit.
6. Sort and send contact and trigger events.
7. Regenerate debug lines if enabled.

**`AfterTicks`** (once per frame): for each interpolated dynamic or kinematic body, write `lerp(previous step pose, current step pose, accumulator / tickRate)` (slerp for rotation) through `SetWorldTransform`, parents before children. Teleports (a diff-detected edit, an `Update` write, a new body) set previous = current.

**Threading:** physics's systems are sequential. Jolt's jobs run on FenrirScheduler's `ThreadPool` through a private `JobSystem` adapter (the spike stepped single-threaded, so this adapter is untested: test it early). Results are identical whatever the thread count.

### FenrirPhysics: layers and filtering

- 32 layers, Godot-style. A body may be in several.
- Two bodies collide when `(A.layers & B.mask) && (B.layers & A.mask)`. Default: layer 0, colliding with everything.
- The filter is read from the body's entity (the `RigidBody` entity, or a standalone static collider's entity) and applies to all its solid colliders. A `CollisionFilter` on a solid child collider is ignored with a warning.
- Every `Trigger` is its own backend body (Jolt's sensor flag is per body), carrying its own filter and following its parent.
- Layer names are fixed at `Install`; a collider's `layers` and `mask` are fields, applied on the next tick.
- **Mapping to Jolt (private):** each distinct `(layers, mask)` in use gets its own object layer, and FenrirPhysics evaluates the rule itself (avoiding the 16-layer limit of Jolt's ready-made mask filter). Broad-phase layers: static, moving, trigger.
- Collision groups (bodies that ignore each other) are not in the foundation.

### FenrirPhysics: contact and trigger events

- Engine events, readable from C++ and C# through the same queue.
- **Contacts:** `ContactBegin { bodyA, bodyB, colliderA, colliderB, point, normal, relativeSpeed }` and `ContactEnd { bodyA, bodyB, colliderA, colliderB }`. The normal points from A to B; `relativeSpeed` is the closing speed along it (impulse is unknown when a contact begins).
- **Triggers:** `TriggerEnter` and `TriggerExit { trigger, otherBody, otherCollider }`. Entities are mapped for C# as `Relationship`'s are.
- **Opt-in:** contacts are reported only when either body has an enabled `ContactEvents`; enabled triggers always report. No *stay* events; `PhysicsWorld::ForEachContact(entity, fn)` answers "touching now".
- **Sleep is hidden** (promised and tested): a contact ended by a body falling asleep is not a `ContactEnd`. Triggers are kinematic sensors kept active, so a sleeping body inside one does not `TriggerExit`.
- **Order:** backend callbacks buffer into per-thread lists during the step. After the step they are merged, sorted by (entity A, entity B, collider A, collider B) and sent, once per step, in `PostTick`.
- **Removal:** removing a body or trigger (destroying its entity, removing its component, disabling it) sends the matching end or exit on the next step, naming the possibly stale entity.

### FenrirPhysics: queries

- On `PhysicsWorld`: `Raycast`, `RaycastAll`, `ShapeCast`, `ShapeCastAll` (with a `CollisionShape` value), `Overlap`, `OverlapPoint`, `ForEachContact`, `IsSleeping`, `Bounds`.
- **Hits:** `RaycastHit { body, collider, point, normal, distance }`, `ShapeCastHit` (the same, with the contact point on the cast shape), `OverlapHit { body, collider }`. No backend-only fields.
- **No allocation:** closest-hit queries return an optional. All-hit queries write into a caller-supplied span, sorted by distance, return the number written and stop when it is full.
- **Filter:** optional `QueryFilter { mask = all, ignoreBody = none, hitTriggers = false }`.
- **State seen:** the last completed step. Changes since then are seen after the next tick's sync.
- **Concurrency:** queries are read-only, so systems running in parallel may query one world at once, never during its sync or step.
- **Starting inside:** a ray starting inside a solid box, sphere, capsule or hull hits at distance 0; triangle-mesh back faces are ignored (promised, tested).

### FenrirPhysics: debug lines

- FenrirPhysics generates them from its own data, not through Jolt's debug renderer. They look and assert the same whatever the backend, and work in every build configuration.
- **Format:** `DebugLine { from, to, colour }`, lines only.
- **Categories (bit flags):**
  - `Colliders`: coloured by motion type, dimmed when sleeping, a distinct colour for triggers;
  - `Contacts`: a point and a short normal;
  - `Velocities`;
  - `Bounds`.
  Colours are fixed defaults.
- Off by default and enabled per world. Regenerated after each step into a world-owned buffer and read as a span valid until the next step. Off costs one flag check per step. Compiled into every build.

### FenrirPhysics: build

- **Jolt arrives** as a git submodule under FenrirPhysics's `libs`, pinned to v5.6.0, added with `add_subdirectory` of Jolt's `Build` directory. It builds as a static library and is linked `PRIVATE`, with its headers as `SYSTEM` includes.
- **Overrides,** set in FenrirPhysics's `libs` CMake before adding Jolt so they stay local:
  - `JPH_BUILD_SHARED_LIBS` OFF, set explicitly;
  - `USE_STATIC_MSVC_RUNTIME_LIBRARY` OFF;
  - `INTERPROCEDURAL_OPTIMIZATION` OFF;
  - `CPP_RTTI_ENABLED` ON;
  - `CPP_EXCEPTIONS_ENABLED` OFF;
  - `JPH_USE_DX12`, `JPH_USE_VK`, `JPH_USE_MTL`, `JPH_USE_CPU_COMPUTE` OFF;
  - `DEBUG_RENDERER_IN_DEBUG_AND_RELEASE` and `PROFILER_IN_DEBUG_AND_RELEASE` OFF;
  - `ENABLE_OBJECT_STREAM` OFF;
  - `CROSS_PLATFORM_DETERMINISTIC` OFF;
  - `ENABLE_INSTALL` OFF;
  - CPU features at their defaults (AVX2 required).
- **Warnings:** the inherited directory compile options are cleared and `ENABLE_ALL_WARNINGS` is OFF, so neither the engine's `-Weverything`/`/W4` nor Jolt's `-Werror`/`/WX` applies to Jolt's sources. FenrirPhysics's own sources keep the engine's flags; backend sources wrap Jolt includes in Jolt's warning-suppression macros.
- **Configurations:** Debug and Release are supported. Other configurations build Jolt without its debug-only checks (documented).
- **Root option** `FENRIR_ENABLE_PHYSICS`, default ON.
- The spike verified these settings in the Clang and MSVC x64 debug and release presets; Linux is unverified.

### FenrirPhysics: scripting extension (ADR-0009)

- **Components:** all six exposed and writable from C#.
- **`CollisionShape` in C#** is hand-written in the contract:
  - an explicit-layout struct: private tag at offset 0, each kind's struct at the union's offset;
  - a read-only `Type`;
  - `[UnscopedRef]` checked views per kind (`Shape.Box`, ...) that throw `InvalidOperationException` for the wrong kind;
  - implicit conversions from each kind.
  It is validated by Iris's existing struct mapping (`IRIS_FIELD_AS` on the native side).
- **Native functions,** in a static class in the `Fenrir.Physics` namespace, each on the active scene's world:
  - forces: `AddForce`, `AddImpulse`, `AddTorque`, `AddAngularImpulse`;
  - queries: `Raycast`/`RaycastAll`, `ShapeCast`/`ShapeCastAll`, `Overlap`, `OverlapPoint`; all-hit forms fill a `Span<T>`;
  - state: `ContactsOf(entity, Span<Contact>)`, `IsSleeping`, `Bounds`;
  - world: gravity get and set, `SetDebugDraw`;
  - collision meshes: `CreateCollisionMesh(ReadOnlySpan<Vector3>, ReadOnlySpan<uint>)`, `ReleaseCollisionMesh`.
- **Events:** the four contact and trigger events, registered as native events with C# mirrors.
- **Robustness:** with no world, no body or a released handle, functions do nothing and return `false` or zero, logging a warning once per call site in debug builds. No exceptions.
- **Where it lives:**
  - the C# contract assembly `Fenrir.Physics`, under FenrirPhysics's managed directory;
  - the C++ native functions in a separate static library `FenrirPhysicsScripting`, so FenrirPhysics never links FenrirScripting.
  Both build only when `FENRIR_ENABLE_PHYSICS` and `FENRIR_ENABLE_SCRIPTING` are on. An app adds `Physics::ScriptingExtension()` to its scripting config.

### FenrirScripting

- **C# event readers:** a C# system holds an `EventReader<T>` field and reads with `foreach (var e in reader.Read())`. Sends still apply when the system returns.
- **World transform from C#:** `entity.WorldTransform` and `entity.SetWorldTransform(t)` as native functions in the built-in extension.
- **Tagged unions:** the README documents the explicit-layout tagged-union pattern; script-defined components may use it by hand. No generator support.

### DemoApp

- The renderer draws with `GetWorldMatrix`, replacing today's one-level composition.
- The editor gizmo shows and edits in world space through `GetWorldTransform`/`SetWorldTransform`.
- A physics scene: a static ground, falling boxes, a kinematic paddle moved in `Tick`, each in a distinct colour. A key cycles the debug-line categories, drawn in one `GL_LINES` pass.
- Physics is installed after the App is created and destroyed before it.
- Event readers replace `ReadEvents` in the camera controller, editor, window and scripting demo.

## Testing Decisions

- **What a good test is:** it drives the engine through its public interface and asserts on observable results: component values, world transforms, events read, query hits, debug lines. Never on private state or which backend calls were made. Physics tests run against real Jolt; there is no fake backend (ADR-0010). Same-build determinism lets tests assert exact simulation results. Physics resting positions use a tolerance for the backend's penetration slop (about 0.02).
- **Seam 1 (new), the App stepped one frame at a time with a chosen delta.** All engine-level and physics behaviour is tested through it, deterministically:
  - phases run in order, the right number of times, including frames with zero and several ticks;
  - the tick cap drops time with a warning;
  - event readers: each reader sees each event once across `Update` and `Tick` readers, late sends, frames with no tick, the missed count, and a deterministic order for parallel sends;
  - stable scenes: entities survive `CreateScene`; `DestroyScene` keeps the active scene;
  - scene resources die with their scene;
  - physics: bodies fall and rest; static, kinematic and dynamic behaviour; teleports; velocity writes; motion-type and shape changes; scale; compound bodies; dynamic bodies under moved and rotated parents; layer and mask rules; contact and trigger events with payloads and order; sleep not ending contacts or triggers; removal sending ends; every query and filter, including rays starting inside; forces; gravity; per-scene worlds pausing and resuming; owner shutdown order; interpolation (blended poses between ticks, true poses restored, teleports snapping, an `Update` edit detected); debug-line categories and counts for known shapes; bit-identical results across two runs, and across single-threaded and multi-threaded stepping.
  Prior art: the scripting tests' `HeadlessApp` and recording logger (which today run real wall-clock frames), and the spike's headless check (`FenrirPhysicsSpikeCheck` on the prototype branch).
- **Seam 2 (existing), `EntityList`'s public interface** in `FenrirECSTests`:
  - component watches: order; both component kinds; destroy and clear report removals; replace records remove then add; several watches; nothing before creation;
  - world-transform functions: deep chains, rotated parents, non-uniform scale, the matrix against the decomposed form, `SetWorldTransform` round trips.
  Prior art: `ComponentTypes`, `DynamicComponents`, `Queries` in `FenrirECSTests`.
- **Seam 3 (existing), the scripting fixture harness** in `ScriptingTests`:
  - C# event readers, replacing `EventsHaveTheSameFrameWindowInBothLanguages` with "each reader sees each event once in both languages";
  - a native component holding a tagged union validates, round-trips through C#, and is rejected with a wrong offset or name;
  - the physics extension's components, native functions, no-op behaviour and events from a fixture script;
  - world transform from C#.
  Prior art: the fixture and extension test assemblies, `ScriptingProbe`.
- The DemoApp physics scene is verified by hand, as the spike was.

## Out of Scope

- 2D physics (Jolt has none).
- A second physics backend (the seam allows one; none is built).
- Double-precision positions (large worlds).
- Determinism across platforms or builds.
- An asset pipeline for collision data.
- Joints and constraints, character controller (including motion-test queries), vehicles, ragdolls and soft bodies: each is its own later spec on this foundation.
- Collision groups (bodies that ignore each other), which come with joints, ragdolls and characters.
- Configurable debug colours, and views of backend internals (broad-phase trees).
- A `[TaggedUnion]` source generator.
- Switching between several DemoApp demo scenes (comes with the first feature-family spec).
- Hiding EnTT from FenrirECS's public headers (a separate task).
- Engine problems found by the spike but not needed here, each a separate task: a second `ConsoleLogger` in one process throws; FenrirScripting does not compile under MSVC; the DemoApp's third-party libraries are untracked local copies.

## Further Notes

- **Decision history:**
  - the wayfinder map `.scratch/fenrir-physics/map.md` and its 19 resolved tickets;
  - ADR-0010, ADR-0011, ADR-0012;
  - research branches `research/jolt-host-requirements`, `research/physics-backend-seams` and `research/engine-event-delivery`;
  - the spike branch `prototype/fenrir-physics-spike`.
- **Risks:**
  - The `ThreadPool` job adapter and its no-deadlock rule (physics's systems wait on jobs queued to the same pool) are unproven.
  - The tagged-union mapping rests on reading Iris's code; its scripting test is the proof, and an additive Iris fix is allowed if it fails (ADR-0006).
- **Engine changes land outside FenrirPhysics:** FenrirApp, FenrirScheduler, FenrirTime, FenrirScene, FenrirECS, FenrirScripting and the DemoApp. Several are independently useful (stable scenes, `DestroyScene`, world transforms, event readers) and can be built before any physics code.
- **Vocabulary** follows `GLOSSARY.md`: FenrirPhysics, physics backend, physics world, rigid body, collider, collision shape, compound body, physics material, trigger, contact, collision layer, scene resource, world transform, component watch, event reader.
