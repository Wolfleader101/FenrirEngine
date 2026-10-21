Status: ready-for-agent

# FenrirPhysics vehicle seam and warnings: one vehicle description across the seam, one way to warn

Origin: the second architecture review of FenrirPhysics (2026-10-21), candidates C ("give vehicles the joint seam's shape") and D ("one module for 'inactive, warned once' and out-of-range values"), grilled to the decisions below. It builds on the world split (PRs #72–#78) and the poses work (PRs #80–#82), all carried by PR #71. It is a refactor: it changes no behaviour, no warning's text or timing, no order, and no public interface of FenrirPhysics. ADR-0010 (the backend is hidden at compile time) and ADR-0015 (vehicles) apply, and neither is reopened; this narrows the private backend seam ADR-0010 describes.

## Problem Statement

**Warnings.** The glossary's *inactive* rule (the component is kept, nothing is made for it, one warning is logged, and it comes back once valid) is written five ways: a flag on the joint record, a set and loop in Characters, a generic lambda in Vehicles for vehicles and wheels, a flag on the differential and anti-roll-bar records, and sets in Bodies for empty `RigidBody`s and colliders beneath a character. Out-of-range values are corrected three ways: Joints' `Corrector` and Vehicles' `WheelCorrector` hand-roll the same `Warn` and `NotNegative`, the vehicle kind's `InRange` hand-rolls a clearable bitmask, and the link checks use a lambda over `WarnOnce`. About 440 of `Vehicles.cpp`'s 1181 lines are correction and comparison.

**The vehicle seam.** Joints cross the backend seam with `CreateJoint`/`UpdateJoint`. Vehicles cross it with ten methods, and the front end diffs seven parts of a vehicle to call six setters (`UpdateVehicle(kind)`, `UpdateVehicleWheel`, `SetVehicleProbe`, `SetVehicleMaxTilt`, `SetVehicleDifferentials`, `SetVehicleAntiRollBars`), keeping eleven fields of shadow state in `VehicleRecord`. Backend knowledge sits in the front end and policy sits in the seam:
- The front end's `Rebuilds` decides that a change of kind, or of a motorcycle's lean limit, rebuilds the vehicle. Both are Jolt's limits (its controller type and `mMaxLeanAngle` are fixed when the vehicle is made).
- `DrivetrainOf`, in `Backend.hpp`, holds FenrirPhysics's rules for what drives each kind (a trailer: a manual gearbox in neutral with defaults), shared by both sides; Jolt adds its own trailer cases on top (differentials ignored, no carry-over, zero engine speed and gear read back).
- `VehicleWheelDesc` carries a whole public `VehicleWheel`, whose `vehicle`, `position`, angles and `state` the backend ignores.

## Solution

**One tracker for inactive components and one helper for out-of-range values**, used by every feature, keeping every message and when it fires.

**The vehicle seam takes the joint seam's shape:** create, update, input, read and destroy. The front end hands the backend a whole corrected, backend-neutral vehicle description on create and on update; the backend patches what Jolt can patch and rebuilds privately when Jolt must, carrying the vehicle's running state over. The front end decides only the rebuilds it alone can see (its wheel entities, its chassis body), and the rules for what drives each kind of vehicle.

## User Stories

1. As an engine maintainer, I want the inactive rule written once, so that a new feature's components behave and warn like the others' without copying a loop.
2. As an engine maintainer, I want out-of-range correction to share one way of warning, so that each component's correction reads as its rules and nothing else.
3. As an engine maintainer, I want `Vehicles.cpp` to be about a vehicle's lifecycle, so that its correction rules and its sync are read apart.
4. As an engine maintainer, I want a vehicle edit to cross the backend seam as one description, so that a new vehicle setting is one field in the description, not a new setter, a new diff and a new shadow field.
5. As an engine maintainer, I want Jolt's rebuild limits to live in the Jolt backend, and FenrirPhysics's drivetrain rules in FenrirPhysics, so that a second backend would not inherit either side's quirks.
6. As an engine maintainer, I want the refactor proven by the existing tests passing unedited, so that I trust it changed nothing a caller can see.

## Implementation Decisions

### The inactive tracker

- A private helper in `src/`, one per kind of component that can be inactive, keyed by entity. Each tick its owner tells it, for each entity, why it is inactive (or that it is valid). It warns when the reason goes from none to some, clears when the entity is valid again, and forgets an entity whose component has gone, so it is warned about afresh if it comes back.
- It takes the component's name and the message pieces the callers write today, so every warning's text is unchanged. Whatever each caller does on top (dropping a constraint, resetting a state) stays the caller's.
- Users: Joints (inactive joints), Characters (inactive character bodies), Vehicles (inactive vehicles and wheels, inactive differentials and anti-roll bars), and Bodies (`RigidBody`s with no collider, solid colliders beneath a character). The `inactive` flags on `JointRecord` and `VehicleLinkRecord` and the hand-written sets and loops go.

### Out-of-range values

- One private helper holds the warn-once bitmask, the "not negative" and "clamp into range" steps, and the "The <component> on entity <n>: " prefix, built on `WarnOnce`.
- Each user states the **lifetime** of what it has warned about, because both exist and tests pin them:
  - **once per component**: joints, wheels, differentials and anti-roll bars warn about each kind of problem once while the component lives (a wheel touched in the drain, or a link removed and re-added, is warned about afresh, as today);
  - **once until valid**: a vehicle's kind clears a problem when it is next applied without it, so a later out-of-range value warns again.
- Users: Joints' `Corrector`, `WheelCorrector`, the engine, gearbox and track corrections and `InRange`, and the differential and anti-roll-bar checks. Unifying the two lifetimes would change when warnings fire, and is not part of this.

### Vehicles' files

- The `Same` overloads, the corrections and the description building move from `Vehicles.cpp` to a second file of the Vehicles module (`VehicleSettings.cpp`, or a name the ticket chooses). Vehicles' interface does not change. Joints keeps its `Corrector` in `Joints.cpp`.

### The vehicle seam

- **Methods:** `CreateVehicle(desc)`, `UpdateVehicle(id, desc)`, `SetVehicleInput(id, input, wake)`, `ReadVehicle(id)`, `DestroyVehicle(id)`. `UpdateVehicleWheel`, `SetVehicleProbe`, `SetVehicleMaxTilt`, `SetVehicleDifferentials`, `SetVehicleAntiRollBars` and the kind-only `UpdateVehicle` go.
- **Update:** the backend re-fills every setting it can patch in place (engine, gearbox, limited-slip ratio, lean switches, tracks, wheels, differentials, anti-roll bars, tilt limit). It swaps its ground probe only when the probe kind changes, or a wheel's radius changes under a sphere probe, as today. Where Jolt cannot patch (a different controller, a motorcycle's lean limit) it rebuilds privately, carrying engine speed, gear (capped to the new gear count), clutch and each wheel's spin over by wheel index, as the front end's carry-over does today, with the same priority.
- **The front end** still diffs the components against what it last applied, to know when to call `UpdateVehicle` and when to wake the chassis; waking stays with `SetVehicleInput`'s `wake`, as today. It keeps the rebuilds that are facts about entities: the wheel set changed (added, removed or reordered wheel entities) or the chassis's body was rebuilt, with its carry-over keyed by wheel entity. `Rebuilds` goes. `VehicleRecord` keeps the last applied description and input in place of its shadow fields.
- **Drive description:** the front end resolves each `VehicleKind` into a backend-neutral description of how the vehicle is driven: its controller (wheeled, motorcycle or tracked), engine, gearbox, limited-slip ratio, differentials, tracks, motorcycle lean settings, and whether it is **powered**. A trailer is an unpowered wheeled vehicle with today's default engine and a manual gearbox in neutral. `DrivetrainOf` leaves `Backend.hpp` for the front end; the backend never names `TrailerVehicle`, and does for an unpowered vehicle what it does for a trailer today (no differentials, no carry-over of engine speed and gear, zero engine speed and gear read back). Input clamping (`Clamped`) reads the resolved gearbox, as it reads `DrivetrainOf` today.
- **Wheel description:** `VehicleWheelDesc` carries exactly what the backend reads: position, steering axis, rest rotation, radius, width, suspension, inertia, angular damping, maximum steer angle, brake and handbrake torques, both tyres, and track side. The vehicle debug lines read the tyres from the corrected wheel the front end keeps.
- **Unchanged:** reading a vehicle back, a sleeping vehicle's kept read (`VehicleRecord::parked`) and Jolt zeroing a sleeping vehicle's wheels, `CapLeanDamping`, and the order of vehicles in the solve.

## Testing Decisions

- The public `PhysicsWorld` tests in `FenrirPhysicsTests` (the vehicle settings, rebuilds, gearbox, differentials, tracks, motorcycles, trailers, parking and switching tests, and every warning check) and the physics scripting tests pin behaviour, warnings, ordering and determinism. Every ticket leaves them passing **unedited**.
- No new tests: the modules and the seam are private (ADR-0010).
- Verification per ticket, per the README: build and test `clang-x64-debug`, and run `scripts/demo-smoke.sh` over the nine physics scenes.

## Out of Scope

- Changing when any warning fires, including unifying the two out-of-range lifetimes.
- Moving other backend-neutral rules out of `JoltBackend.cpp` (contacts, characters, layers): the review's candidate I.
- Component descriptions, event scoping, DemoScenes and the scripting extension (candidates E–H).
- Any change to the public FenrirPhysics interface, the scripting extension or the DemoApp.

## Further Notes

- **Delivery:** two tickets in `issues/`, one stack (both touch `packages/FenrirPhysics/src/`, and `Vehicles.cpp` in particular): 01 (warnings, D) then 02 (the vehicle seam, C). The plan's branch, `claude/physics-vehicles-spec`, targets `claude/physics-world-split-spec`, which PR #71 carries into `dotnet-scripting` once the physics API is final.
- No ADR: private, reversible, no behaviour. No glossary change.
- When ticket 02 merges, the commit that records the spec built deletes `.scratch/fenrir-physics-vehicle-seam/`.
