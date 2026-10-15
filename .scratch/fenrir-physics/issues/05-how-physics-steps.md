# How does physics step?

Type: grilling
Status: resolved
Blocked by: 04, 14, 15

## Question

How does FenrirPhysics run inside the app's frame?

- One physics step per `Tick`, or collision substeps inside it? Is the step rate `Time::tickRate`, or physics's own?
- Where do the sync systems run (ECS to physics before the step, physics to ECS after)? `SchedulePriority` has a single `Tick` phase: does FenrirScheduler need ordering inside `Tick` or new phases, and how is that kept usable by other packages?
- Rendering runs at frame rate between ticks: is there transform interpolation (a separate rendered/interpolated transform, or none), and who owns it?
- How do game systems that run in `Tick` see consistent state relative to the step (before or after)?
- How does stepping stay deterministic (see the map's Notes), given the threading answer from "What does Jolt require of a host engine?"?

## Answer

- **New fixed-step phases:** the fixed loop becomes `PreTick` → `Tick` → `PostTick`, each run once per fixed step (`SchedulePriority` gains `PreTick` and `PostTick`). Game logic stays in `Tick`; physics runs entirely in `PostTick` (as Avian runs in Bevy's `FixedPostUpdate`); `PreTick` is for other packages (e.g. an input snapshot per step). Generic phases were chosen over adding before/after ordering to the scheduler. A `Tick` system sees the previous step's results and its changes go into the next step, matching "queries see the last completed step".
- **One step per tick at the app's rate:** `dt = Time::tickRate`, no physics rate of its own; collision steps `ceil(tickRate × 60)` (one at 60 Hz or faster), per Jolt's guidance. A constant `dt` suits warm starting.
- **Ticks per frame are capped** (a FenrirTime/FenrirApp fix for the foundation spec): `Time` gains `maxTicksPerFrame` (default 5); time owed beyond it is dropped from the accumulator with a warning, so a long frame slows the game briefly instead of spiralling.
- **`PostTick` order**, for the active scene's world, all sequential:
  1. Drain the component watches; create and destroy bodies in one batch by each entity's net effect.
  2. Diff components and world transforms against physics's private copies; push differences (kinematic targets, teleports, velocities, shapes, filters, motion types).
  3. Apply queued forces and impulses.
  4. Step (`dt`, collision steps).
  5. Write back dynamic bodies' poses (`SetWorldTransform`) and velocities (`RigidBody`), updating the private copies so these writes are not taken for game edits.
  6. Sort and send contact and trigger events.
  7. Regenerate debug lines if enabled.
- **Threading:** physics's systems are sequential, on the thread that runs the App (a pool worker waiting on jobs queued to its own pool risks deadlock). Jolt's jobs run on FenrirScheduler's `ThreadPool` through a private `JobSystem` adapter; `ThreadPool` gains `ThreadCount()`. `PhysicsConfig` can choose single-threaded stepping for tests and debugging. Results are identical either way (Jolt is deterministic whatever the thread count); determinism rests on the fixed order above, the watches' operation order and a stable diff order.
- **Transform interpolation** is not in the foundation's stepping; it is its own question, "Is there transform interpolation, and who owns it?".
