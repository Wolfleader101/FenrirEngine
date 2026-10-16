---
status: accepted
---

# FenrirPhysics hides its physics backend at compile time

FenrirPhysics simulates with JoltPhysics today, but we may replace it (Rapier, Box3d, an engine of our own). When we do, FenrirPhysics's public interface must not change, and code that uses it must not even recompile. So no public FenrirPhysics header names or includes Jolt, and FenrirPhysics links Jolt `PRIVATE`, so neither Jolt's include path nor its compile definitions reach FenrirPhysics's users.

This goes further than ADR-0008, which keeps EnTT out of other packages' source but still lets its headers reach their compilation. That leak is a known smell to clean up, not a precedent: a physics interface is not template-heavy, so hiding the backend completely costs little.

## The seam

- **Build-time, in one place.** A private, uninstalled header declares a concrete backend class with no virtual calls. Its one implementation lives in `src/Backend/Jolt/`, selected by CMake, and that directory is the only code that includes Jolt. The rest of FenrirPhysics (ECS sync, event ordering, queries) calls the backend class. Replacing Jolt means writing `src/Backend/<Other>/`.
- **Entities and components are the public identity.** Game code configures physics with components on entities (rigid body, collider, ...; their layout is a later decision) and names bodies by `Entity`: queries return entities, events name entities, forces are applied to entities. There is no public body handle. FenrirPhysics maps entities to backend ids privately.
- **Install owns the backend's process-wide state.** `Physics::Install(App&, PhysicsConfig)` returns the object that owns what physics set up (the backend's allocator, type registration and log routing into FenrirLogger, its systems, its settings), as `Scripting::Install` does for scripting. Destroying it tears them down.
- **Public math is `Fenrir::Math`.** Conversion to the backend's types is private.

## What the other engines leaked, and our answer

A survey of Godot, O3DE, Bevy (Avian, bevy_rapier) and Unreal (branch `research/physics-backend-seams`) found the same leaks in each. Our answers:

- **Backend tuning settings:** refused. Public settings are backend-neutral: gravity, step settings, capacities as plain limits.
- **A native escape hatch** (`GetNative()`, reachable backend types): refused. A feature that needs the backend gets a neutral API in FenrirPhysics, as ADR-0008 asks of the ECS.
- **Behaviour differences between backends:** accepted. FenrirPhysics promises behaviour only where its tests pin it; anything else is the backend's.
- **Threading shaped by the backend:** contained. Public calls are made from the thread that drives the App, never during a step. The backend's job system runs privately on FenrirScheduler's `ThreadPool`.

## Considered options

- **A runtime backend interface** (an abstract class, backends chosen at start-up, as Godot and O3DE do). Rejected: with one backend it is a hypothetical seam, and every backend call would pay a virtual call to keep a choice no build makes. A second backend that must ship alongside Jolt would revisit this.
- **No internal seam** (Jolt used from any private source file). Rejected: the public interface would still hide Jolt, but replacing it would mean touching all of FenrirPhysics's private code instead of one directory.
- **A test-only fake backend.** Rejected: Jolt runs in-process, fast and deterministic on one machine, so FenrirPhysics's tests go through its public interface against real Jolt and may assert exact simulation results and debug rendering data.
