# FenrirPhysics

Label: wayfinder:map

## Destination

A set of specs under `.scratch/fenrir-physics/specs/`, each ready for `/to-tickets`, plus ADRs for the hard-to-reverse calls (where the backend seam goes, which side owns body state). The first is the **foundation spec**: the backend seam over Jolt, rigid bodies and shapes, collision layers, stepping, contact and trigger events, queries, debug rendering data, the physics scripting extension, and a physics demo scene in the DemoApp. Then one spec per feature family (constraints and joints, character controller, vehicles, ragdolls, soft bodies), each building on the foundation. The foundation spec is ticketable as soon as its own decisions close, without waiting on the feature families.

## Notes

- Vocabulary: **FenrirPhysics**, **physics backend**, **physics world** (see `GLOSSARY.md`, "FenrirEngine physics"). In this map, *component*, *system*, *event* and *entity* carry their ECS meanings from the glossary.
- **The backend is hidden at compile level.** No public FenrirPhysics header names or includes Jolt; Jolt is a private link dependency. Replacing it (Rapier, Box3d, an in-house engine) must not change FenrirPhysics's public interface or recompile its callers. This goes further than ADR-0008, whose EnTT header leak is a known smell, not a precedent.
- **Packages stay independent.** FenrirPhysics depends only on the core (App, Scheduler, Time, ECS, Scene, Math, Logger). Its C# support is a scripting extension (ADR-0009) that lives in FenrirPhysics and is built only when scripting is enabled. Debug rendering data is plain data that physics produces; no renderer depends on physics, and physics depends on no renderer.
- **Determinism:** the same inputs give the same simulation on the same machine and build.
- **Conventions:** meters, Y-up, right-handed (matching glm and the DemoApp camera); single-precision floats, like the rest of the engine.
- **Existing hooks:** `App::Run` runs `SchedulePriority::Tick` in a fixed-step accumulator loop (`Time::tickRate`), built for systems like physics. `Transform` is local to its parent through `Relationship` (`FenrirECS/Entity.hpp`); the physics backend simulates in world space.
- Skills: grilling tickets call `grilling` and `domain-modeling`; seam and module-shape questions also use `codebase-design`. Research tickets call `research` and put their findings on a `research/<name>` branch.
- Plan, don't do: tickets produce decisions, not code (the spike in "Spike: a box dropping onto a plane through the proposed seam" is throwaway).

## Decisions so far

<!-- one line per resolved ticket: [title](issues/NN-slug.md): gist -->

- [What does Jolt require of a host engine?](issues/01-what-jolt-requires-of-a-host.md): Jolt v5.6.0 can run on our `ThreadPool`; bodies are locked during a step; listeners run on job threads unordered and must be buffered; same-binary determinism holds given ordered API calls; much is process-global; several build defaults must be overridden.
- [How do other engines hide their physics backend?](issues/02-how-other-engines-hide-their-backend.md): seams are runtime interfaces (Godot, O3DE) or build-time typedefs (Unreal); all own opaque handles, simulate in world space with an adapter to the local hierarchy, and buffer contacts for one-thread delivery after the step; the usual leaks are backend tuning settings, documented behaviour differences, native pointers, and threading shaped by the backend.
- [Where does the backend seam go?](issues/03-where-the-backend-seam-goes.md): ADR-0010. Build-time seam: a private backend class with its only implementation in `src/Backend/Jolt/`; entities and components are the public identity; no tuning settings or escape hatch; tests against real Jolt; `Physics::Install` owns the backend's process-wide state.
- [How is physics modelled in the ECS?](issues/04-physics-in-the-ecs.md): ADR-0011. One `RigidBody` with a motion type; one `Collider` per entity holding a `CollisionShape` tagged union by value, compounds through child entities; optional `PhysicsMaterial`, `CollisionFilter`, `Trigger`; hull and mesh data cooked from caller-supplied vertices behind a FenrirPhysics handle; the ECS owns static and kinematic bodies, physics owns dynamic ones; adds and removes through new FenrirECS notifications, edits found by diffing each tick.
- [What is the physics world's lifecycle?](issues/06-physics-world-lifecycle.md): one world per scene, a scene resource (a new neutral per-scene slot in FenrirScene), paused while inactive, destroyed with its scene; reached by `PhysicsWorld::Of(scene)`; `PhysicsConfig` gives defaults, gravity is per world, capacities fixed at creation; FenrirApp fixes for stable scene addresses and `DestroyScene` are foundation prerequisites.
- [How do collision layers and filtering work?](issues/07-collision-layers-and-filtering.md): Godot-style 32-bit layer and mask on `CollisionFilter`, symmetric rule; the filter belongs to the body, triggers carry their own; layers optionally named in `PhysicsConfig`; private mapping to Jolt object layers; collision groups left to the feature families.
- [How do contact and trigger events reach game code?](issues/08-contact-and-trigger-events.md): engine events `ContactBegin`/`ContactEnd` (opt-in with a `ContactEvents` tag) and `TriggerEnter`/`TriggerExit`, no stay events; plain-data payloads naming body and collider entities; sleep hidden; sorted and sent after each step; reading each event once handed to the engine ("How does a system read each event exactly once?").
- [How do other engines deliver events to their systems?](issues/17-how-engines-deliver-events.md): FenrirApp's readers see events twice per frame, zero-or-many times in `Tick`, and race under parallel systems; Bevy gets exactly-once from per-reader cursors and gates its buffer swap on the fixed step; Unity and Godot deliver physics events once per step change; options listed without a choice.
- [What does the query API look like?](issues/09-physics-queries.md): raycast, shape cast (with a `CollisionShape` value), overlap and point overlap on `PhysicsWorld`, closest or all; plain-data hits naming body and collider entities; caller-supplied spans, no allocation; `QueryFilter` with mask, ignored body, triggers off by default; sees the last completed step; safe from parallel systems.
- [What does debug rendering data look like?](issues/10-debug-rendering-data.md): FenrirPhysics generates coloured `DebugLine`s itself (not Jolt's renderer) for colliders, contacts, velocities and bounds; off by default, enabled per world, regenerated after each step, read as a span; in every build; tests use new `IsSleeping`/`Bounds` queries; the DemoApp draws them with `GL_LINES`.

## Not yet specified

- **DemoApp demo scenes:** switching between several demo scenes (one per feature family) in the DemoApp, and what the first physics scene shows.
- **Feature-family specs:** constraints and joints, character controller, vehicles, ragdolls, soft bodies. Each becomes tickets once the foundation settles the body, shape and stepping vocabulary they build on.

## Out of scope

- 2D physics: Jolt has no 2D support.
- A second physics backend: the seam must allow one, but none is built.
- Large worlds (double-precision positions): positions are float; the seam must not assume Jolt's types, so this stays addable later.
- Determinism across platforms or builds.
- An asset pipeline for collision data: callers supply vertex and index data ("How is physics modelled in the ECS?").
