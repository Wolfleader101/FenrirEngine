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

## Not yet specified

- **Mesh collider data:** a triangle-mesh or convex-hull collider needs vertex data, but FenrirPhysics may not depend on the renderer's `Model`. Where that data comes from, and who owns it.
- **DemoApp demo scenes:** switching between several demo scenes (one per feature family) in the DemoApp, and what the first physics scene shows.
- **Feature-family specs:** constraints and joints, character controller, vehicles, ragdolls, soft bodies. Each becomes tickets once the foundation settles the body, shape and stepping vocabulary they build on.

## Out of scope

- 2D physics: Jolt has no 2D support.
- A second physics backend: the seam must allow one, but none is built.
- Large worlds (double-precision positions): positions are float; the seam must not assume Jolt's types, so this stays addable later.
- Determinism across platforms or builds.
