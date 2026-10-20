# 06: Disabled and inactive vehicles

**What to build:** A wreck is one field write, and every misuse of `Vehicle` or `VehicleWheel` is visible with one warning and never crashes (spec: "FenrirPhysics: the components" (inactive, switching), "state" (reset), "the step in `PostTick`" (disabled or inactive), "edges" (the chassis loses its body), "ground, grip and other bodies" (joints)).

- **Disabled** (`enabled = false`): the backend vehicle is removed, leaving a plain rigid body resting on its chassis; wheel entities stay where they last were; `state` on the vehicle and its wheels resets. Re-enabling builds the vehicle fresh with nothing carried over.
- **Inactive vehicle** (component kept, no backend vehicle, one warning on each transition to invalid, back by itself once valid, built fresh): no dynamic `RigidBody` with a usable collider, beside a `CharacterBody`, or a chassis turned kinematic or static or losing its `RigidBody` or usable collider. A car made kinematic for a cutscene restarts at idle in neutral.
- **Inactive wheel:** naming something that is not a vehicle, sitting on the chassis entity, or sharing its entity with a `RigidBody`, `CharacterBody` or any `Collider` (triggers included). It is left out of its vehicle (a rebuild) with one warning.
- **Joints:** a `Joint` naming a wheel entity names a non-body and is inactive under the joints spec's edge rule; nothing new, tested here.
- **No structural changes by physics:** nothing in this ticket adds or removes components.
- **DemoApp (C++):** 4 toggles the car's `enabled` (a plain rolling box when off); 9 makes the car's chassis kinematic and back (one warning, then fresh at idle in neutral). 9's warning is the other expected one.

**Blocked by:** 01 (Vehicle tracer bullet).

**Status:** resolved

- [x] `FenrirPhysicsTests`: a disabled vehicle is a plain body, its `state` and its wheels' states reset and its wheel entities left in place; re-enabled, it starts fresh.
- [x] A vehicle is inactive without a dynamic body, with no usable collider and beside a `CharacterBody`, with one warning per transition and reactivation once valid; a chassis made kinematic and back starts fresh at idle in neutral.
- [x] A wheel is inactive naming a non-vehicle, on the chassis entity, and beside a `RigidBody`, a `CharacterBody` or a trigger `Collider`, each with one warning.
- [x] A `Joint` naming a wheel entity is inactive.
- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] With 4 off the car is a plain rolling box; 9 gives one warning and on return the car is at idle in neutral (checked by hand).

**Resolved by:** PR https://github.com/Wolfleader101/FenrirEngine/pull/50 (branch claude/vehicles-disabled-inactive-8053c2, not yet merged into dotnet-scripting). The proof is `FenrirPhysicsTests`' `[vehicles][inactive]` cases (`Tests/VehicleSwitching.cpp`) and the `FENRIR_DEMO_SCENE="Vehicles (C++)" FENRIR_DEMO_KEYS=1` smoke run, which taps 4 and 9 twice each; the last box (watching the box roll and the car return at idle by hand) is left for a person. Choices the ticket left open:
- Validity is checked in step 2, at the start of `SyncVehicles`, after the bodies are synced: `WhyNoChassis` and `WhyNoWheel` in `src/Vehicles.cpp` give each reason, and `ChassisBodyOf` and `WheelsByVehicle` are built on them. Each Vehicle and each VehicleWheel is warned about once per transition to inactive, as characters are; a disabled vehicle is still warned about if it is also inactive.
- A chassis whose collider carries a `Trigger` is inactive: its RigidBody gets no solid collider from it.
- A wheel whose `vehicle` is empty is inactive with a warning, like any wheel naming something that is not a vehicle.
- Disabling or going inactive drops the backend vehicle, which wakes the chassis, so a parked car switched off settles onto its box. Re-enabling, or valid again, takes the "made" path with an empty record, so nothing carries over: the new vehicle starts at idle (the engine's `minRpm`) in neutral.
- The DemoApp's 9 zeroes the chassis's velocities as it turns it kinematic, so the car holds still as for a cutscene.
