Status: ready-for-agent

# FenrirPhysics world split: feature modules on one pipeline, over tracked component sets

Origin: the architecture review of FenrirPhysics and its integration (2026-10-21), candidates 1 ("split the physics world into feature modules on one pipeline") and 2 ("deepen `ComponentWatch` into a tracked set"), grilled to the decisions below. It is a refactor: it changes no behaviour, no message and no order, and no public interface of FenrirPhysics. ADR-0008 (FenrirECS owns the ECS library), ADR-0010 (the backend is hidden at compile time) and ADR-0011 (physics components are plain data that physics diffs) apply, and none is reopened. The vehicle seam (the review's candidate 3: seven backend calls for one vehicle edit) is left as it is, for a later change once vehicles have one module.

## Problem Statement

FenrirPhysics's world is one private struct, `PhysicsWorld::Impl` (`src/WorldImpl.hpp`, about 60 members). Bodies, joints, character bodies, vehicles, contact events, interpolation and debug lines are all methods on it, spread over `PhysicsWorld.cpp`, `Joints.cpp`, `JointState.cpp`, `JointBreaking.cpp`, `Characters.cpp`, `Vehicles.cpp` and `DebugLines.cpp`. Every feature can read and write every other feature's records, and several do:

- `SyncBodies` drives the other features: it drains the joint and vehicle watches itself, and calls `RemoveJointsOf`, `RemoveVehiclesOf`, `DropCharacters` and `MakeCharacters`. Joints and vehicles read the body records back, so Bodies ↔ Joints and Bodies ↔ Vehicles are cycles.
- `WriteBack` writes bodies and characters back together, parents before children; `Blend` blends them in one depth-sorted list; `BlendWheels` and `AdvanceWheels` read the chassis's interpolation record, and rely on running before `AdvancePoses` clears it.
- Contact events from bodies and characters share one set of reported contacts and one list of begins and ends.
- The step's order is kept in comments, with lettered sub-steps ("step 3b", "step 5b") added by each feature family.

The vehicle tickets all landed in `Vehicles.cpp` (1195 lines), against this shared state. The next families (ragdolls, soft bodies) would land the same way.

Each feature also repeats the same bookkeeping for each component type it watches: a `ComponentWatch` member, a `ForEachWith` scan of what was there when the world was made (a watch sees only later changes, so forgetting the scan silently misses bodies), a `std::set` of the entities with the component now, and a drain loop that inserts or erases by "alive and has it". Physics does this for nine component types, in five near-identical loops. Physics is `ComponentWatch`'s only user.

## Solution

**FenrirECS gains the tracked component set** (`TrackedComponents`; glossary: *tracked component set*): the entities in one scene that have one component type, as of its last drain, in entity order, kept from a component watch and a scan made when the set is made. Each drain names the entities whose component came or went since the one before, each once, even if it came back. `ComponentWatch` stays as it is. Physics moves its nine watches onto tracked sets.

**The physics world becomes a pipeline over feature modules.** Each feature is a private class owning its tracked sets, its records and its warnings: **Bodies**, **Characters**, **Joints** and **Vehicles**, plus **Poses** (the Transforms physics writes, and interpolation) and **Contacts** (contact and trigger events). The world owns the backend and the modules, and `Step` calls them by name, stage by stage, so the order is code. A module never calls another module's hooks: whatever crosses features (the bodies about to be destroyed, the entities whose body components changed) is passed by `Step` as an argument. Dependencies run one way: Characters, Joints and Vehicles read Bodies through a narrow const view, and Bodies reads none of them.

## User Stories

1. As an engine maintainer, I want each physics feature's state behind its own module, so that a vehicle bug is found and fixed in the Vehicles module without reading body, character or contact code.
2. As an engine maintainer, I want `Step` to name its stages in code, so that the order of the pipeline is read from one function, not reconstructed from comments and lettered sub-steps.
3. As an engine maintainer, I want the next feature family (ragdolls, soft bodies) to be a new module and new stage calls, so that adding it does not touch the existing features' records.
4. As an engine maintainer, I want watching a component type to come with its initial scan and its live set, so that I cannot build one without the other.
5. As an engine maintainer, I want the refactor proven by the existing tests passing unedited, so that I trust it changed nothing a caller can see.

## Implementation Decisions

### FenrirECS: the tracked component set

- A new public type, `TrackedComponents`, in its own header in `FenrirECS`, built on `ComponentWatch` (which keeps its "each change is a fact" contract, its header and its tests). It is made from an `EntityList` and a `ComponentType`, native or dynamic, the way `EntityList::Watch` is.
- **Made:** it scans the list once for the entities that have the type, so the set starts full.
- **Drain:** returns the entities whose component of the type came or went since the last drain (or since it was made), each once, in entity order, however many changes each had. A component removed and added again, or replaced (`AddComponent` on an entity that has it), counts. Then the live set is brought up to date: an entity is in it if it is alive and has the type now.
- **The live set:** the entities with the type as of the last drain (or the scan), in entity order, readable as a range and by membership. Between drains it does not change.
- It holds no per-entity data. Each physics feature keeps its own records, keyed by entity.
- Move-only, like `ComponentWatch`; it may outlive its list and then records nothing more.
- Several tracked sets of one type are independent (Bodies and Characters each track `CharacterBody`).
- ADR-0011's 2026-10-16 amendment says physics drains `ComponentWatch`es; it gains a sentence that physics drains them through tracked component sets.

### FenrirPhysics: the modules

All are private classes in `Fenrir::Detail`, one header and one `.cpp` each, flat in `src/` (`Bodies`, `Characters`, `Joints`, `Vehicles`, `Poses`, `Contacts`, `DebugCanvas`). `Vehicles.cpp`, `Joints.cpp` and `Characters.cpp` keep their names, so their history stays readable; `JointState.cpp` and `JointBreaking.cpp` may fold into `Joints.cpp` or stay as further files of that module. `WorldImpl.hpp` ends as the world: the backend, the modules, `Step`, `Restore`, `Blend` and the members `PhysicsWorld`'s public methods need. Each module holds references to the `EntityList`, the `ILogger` and the `Backend::World` it is given, and const references to the modules below it.

- **Bodies.** Owns the `RigidBody`, `Collider`, `Trigger` and `CharacterBody` tracked sets (the last because a collider's body changes when a character body comes or goes), the body records (`Record`, `Part`), the empty-`RigidBody` and character-child warnings, the force and impulse queue, and the rules for which colliders make which body: `BodyEntityOf`, `ResolveBody`, `AttachedBodyOf`, character colliders and colliders beneath a character (component rules; they read no character state). Its const view gives the others what they read today: whether an entity is a made body entity, its backend id, world transform, motion and whether it is active, and `FindBody`/`ResolveBody` for queries and events.
  - **The rebuild is two calls.** `Plan` drains Bodies' tracked sets, works out the assembly and names the bodies to destroy and the entities whose body components came or went. `Step` then lets Joints, Vehicles and Characters drop what they had on those bodies, and `Apply` destroys and makes the bodies in one batch each, in entity order. Characters are made after.
  - **Edits:** the diff of each body against its record (`PushEdits`), and the queued pushes (`ApplyPushes`).
- **Characters.** Owns its `CharacterBody` tracked set, its records, the inactive-character warnings, `Drop` (given the entities whose body components changed), `Make`, its edits (teleports and turns, material and filter), its moves before the step, and writing back its velocity and state. It hands its new world poses to Poses. It exposes each character's end-of-move contacts to Contacts.
- **Joints.** Owns its `Joint` tracked set, its records, the index from bodies to joints, `DropOn(destroyed bodies)`, its sync (making, recreating and updating constraints, its `Corrector`), measuring loads, breaking, `GetJointState`, and sending `JointBroken` itself, right after Contacts sends. It reads bodies through Bodies' const view and step poses through Poses.
- **Vehicles.** Owns its `Vehicle`, `VehicleWheel`, `VehicleDifferential` and `VehicleAntiRollBar` tracked sets, its records and link records, `DropOn(destroyed bodies)`, its sync, its write-back, and its wheels' poses, blending and restoring (wheels are blended in the chassis's axes, which only Vehicles knows). It reads the chassis through Bodies' const view and the chassis's interpolation record through Poses. Its seven backend calls are unchanged.
- **Poses.** Owns the Transforms physics writes for bodies and characters and how they are shown between ticks: the `Shown` record per entity, the write-back of step poses parents before children (bodies and characters in one order), `Advance`, `Restore` (a body's turn is a teleport, a character's is not) and `Blend` (one depth-sorted list over bodies and characters, with the carried-at-step-pose rule). Bodies and Characters hand it each entity's new world pose; their own fields (`RigidBody` velocities, character `velocity` and `state`) stay with them. Teleports mark a snap through Poses. Vehicles reads a chassis's `Shown` from it, and wheel write-back runs before `Advance`, as today.
- **Contacts.** Owns the reported contacts and overlaps and the character touches, turns the backend's touch changes and touches and each character's end-of-move contacts into `ContactBegin`, `ContactEnd`, `TriggerEnter` and `TriggerExit`, sorted and sent as today, and answers `ForEachContact`.
- **DebugCanvas.** Holds the debug lines a step makes, the colours and line helpers, the shape-edge cache and drawing a collider part (shared by Bodies and Characters). Each module has `Draw(DebugCanvas&) const` for its own categories; the world switches categories and calls them in today's order. `DebugLines.cpp` dissolves into the modules and the canvas.
- **Queries** (`Queries.cpp`) read Bodies' const view and the backend, nothing else.
- **Shared helpers** move to private headers under `src/`: one `ParentOf` and one `DepthOf` (today three and two copies), one `FilterOf` (two), the "RigidBody and not a trigger" ancestor test (three), and a warn-once bitmask helper for `Corrector`, `WheelCorrector` and the vehicle link warnings. FenrirECS's interface does not grow for them.

### FenrirPhysics: the stages of `Step`

`Step` calls the modules in this order, which is today's order; no stage moves.

| Stage | Calls |
| --- | --- |
| Drain and rebuild | `bodies.Plan()`; `joints.Drain()`, `vehicles.Drain()` (removed joints and vehicles lose their constraint or vehicle); `joints.DropOn(destroyed)`, `vehicles.DropOn(destroyed)`; `characters.Drop(changed)`; `bodies.Apply()`; `characters.Make()` |
| Edits | `bodies.PushEdits(dt)`; `characters.PushEdits()`; `joints.Sync(dt)`; `vehicles.Sync()`; `bodies.ApplyPushes()` |
| Moves | `characters.Move(dt)` |
| Step | `backend.Step(dt, collisionSteps)` |
| Write-back | bodies and characters through Poses, parents first; idle characters' state reset; `vehicles.WriteBack(dt)` |
| Joints after the step | `joints.Measure()`; `joints.Break()` |
| Events | `contacts.Send(app)`; `joints.SendBroken(app)` |
| Shown poses | `poses.Advance()` |
| Debug lines | each module's `Draw`, if any category is on |

The names are a guide; the ticket that builds each module chooses its methods' exact names, and the comments that numbered steps ("step 2", "step 3b", "step 5b") go.

## Testing Decisions

- A good test here is one that already exists: the public `PhysicsWorld` tests in `FenrirPhysicsTests` and the physics scripting tests pin behaviour, messages, ordering and determinism (bit-identical runs in the bodies, events, interpolation, joints, characters, vehicles and threading tests). Every ticket leaves them passing **unedited**; a test that needs editing means the change is not a refactor.
- The modules get no tests of their own: ADR-0010 puts FenrirPhysics's tests at its public interface against real Jolt, and the modules are private.
- The tracked component set gets `FenrirECSTests` cases, beside the `ComponentWatch` ones: the initial scan; each touched entity reported once and in entity order; a removal and re-add, and a replace, counting as touched; the live set after adds, removes, destruction and clearing; a dynamic component type; two independent sets on one type.
- Verification per ticket, per the README: build and test `clang-x64-debug`, and run the DemoApp smoke runs (`FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1`) of the physics scenes: `"Physics (C#)"`, `"Joints (C++)"`, `"Joints (C#)"`, `"Character (C++)"`, `"Character (C#)"`, `"Vehicles (C++)"`, `"Motorcycle (C++)"`, `"Tank (C++)"` and `"Vehicles (C#)"`. Each ticket names the ones it must run. CI checks the other presets.

## Out of Scope

- The vehicle seam (seven backend calls for one vehicle edit) and moving backend-neutral rules out of `JoltBackend.cpp`: later changes, which this split makes easier.
- Any change to the public FenrirPhysics interface, to `Physics.cpp`'s systems, to the scripting extension or to the DemoApp.
- Exposing hierarchy walks (`ParentOf`, `DepthOf`) from FenrirECS.
- A virtual feature interface over the modules: the modules need different stages, so `Step` calls each by name.

## Further Notes

- **Delivery:** six tickets in `issues/`, built as one stack (every ticket touches `packages/FenrirPhysics/src/`), each PR targeting the previous ticket's branch, the first targeting `dotnet-scripting`. Each ticket leaves everything building and passing, with code not yet moved still on the world.
- **PR #69** (`dotnet-scripting` into `main`) waits only for ticket 01, the one change to an interface outside FenrirPhysics. Tickets 02–06 are private to FenrirPhysics and may land after it.
- No ADR: the split is private, reversible and changes no behaviour, and the tracked component set extends ADR-0011's watch without changing it.
- When ticket 06 merges, the commit that records it built deletes `.scratch/fenrir-physics-world-split/`.
