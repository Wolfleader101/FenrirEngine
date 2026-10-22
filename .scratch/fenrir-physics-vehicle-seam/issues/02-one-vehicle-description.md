# 02: One vehicle description across the backend seam

**What to build:** the vehicle seam becomes create, update, input, read and destroy, each create and update taking a whole corrected, backend-neutral description; the backend rebuilds privately for its own limits, and the front end resolves what drives each kind (spec: "The vehicle seam").

- **Seam:** `CreateVehicle(desc)`, `UpdateVehicle(id, desc)`, `SetVehicleInput(id, input, wake)`, `ReadVehicle(id)`, `DestroyVehicle(id)`. The kind-only `UpdateVehicle`, `UpdateVehicleWheel`, `SetVehicleProbe`, `SetVehicleMaxTilt`, `SetVehicleDifferentials` and `SetVehicleAntiRollBars` go.
- **Jolt update:** re-fills every patchable setting; swaps the ground probe only when its kind changes or a wheel's radius changes under a sphere probe; rebuilds privately for a different controller or a motorcycle's lean limit, carrying engine speed, gear (capped to the new gear count), clutch and wheel spin by wheel index, with the same priority.
- **Front end:** still diffs to decide when to update and when to wake (through `SetVehicleInput`'s `wake`); still rebuilds, with carry-over by wheel entity, when the wheel set or the chassis's body changes. `Rebuilds` goes; `VehicleRecord` keeps the last applied description and input instead of its shadow fields.
- **Drive description:** the front end resolves `VehicleKind` into controller, engine, gearbox, limited-slip ratio, differentials, tracks, lean settings and `powered` (false for a trailer, with today's default engine and a manual gearbox in neutral). `DrivetrainOf` leaves `Backend.hpp`; the backend never names `TrailerVehicle` and treats an unpowered vehicle as it treats a trailer today. `Clamped` reads the resolved gearbox.
- **Wheel description:** `VehicleWheelDesc` carries only what the backend reads (position, steering axis, rest rotation, radius, width, suspension, inertia, angular damping, maximum steer angle, brake and handbrake torques, both tyres, track side); the debug lines read tyres from the front end's corrected wheel.
- Reading back, parked vehicles, `CapLeanDamping` and solve order unchanged. Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited, every vehicle test included.

**Blocked by:** 01 (One inactive tracker and one way to correct out-of-range values).

**Touches:** `packages/FenrirPhysics/src/` (`Backend/Backend.hpp`, `Backend/Jolt/JoltBackend.cpp`, `Vehicles.*`, `VehicleSettings.cpp`).

**Status:** resolved

- [x] `Backend::World` has five vehicle methods; `Backend.hpp` has no `DrivetrainOf` and no `VehicleWheel` inside `VehicleWheelDesc`; `JoltBackend.cpp` does not name `TrailerVehicle`.
- [x] The front end has no `Rebuilds`; `VehicleRecord` holds the last applied description and input.
- [x] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [x] `scripts/demo-smoke.sh` passes all nine physics scenes.

**Resolved by:** branch claude/vehicle-one-description, into claude/physics-vehicles-spec. `Backend::World`'s vehicle methods are `CreateVehicle`, `UpdateVehicle(id, desc)`, `SetVehicleInput`, `ReadVehicle` and `DestroyVehicle`. `VehicleDesc` carries a `VehicleDriveDesc` (`controller`: wheeled, motorcycle or tracked; `powered`; engine, gearbox, limited-slip ratio, lean, tracks) in place of the kind, and `VehicleWheelDesc` carries flat fields only. `VehicleSettings.cpp`'s `DriveDescOf` corrects a kind and resolves it (a trailer: unpowered, the default engine, Manual), replacing `InRange` and `DrivetrainOf`; `Clamped` reads the drive description. Jolt's `UpdateVehicle` re-fills every patchable setting and makes a new tester only for a new probe kind or a resized wheel under a sphere probe. It rebuilds in the same slot (`Impl::MakeVehicle`/`UnmakeVehicle`, the same Jolt calls in the same order as the old destroy and create) for a different controller, a change of `powered`, or a motorcycle's lean limit. It carries over what `ReadVehicle` reads, the gear capped to the new count, and returns true. The front end then shows the vehicle as new (wheels posed at once, snapped, `parked` cleared), as its own rebuild did. Two points for review:
- **`powered` rebuilds as well.** Wheeled↔trailer was a rebuild before, and Jolt sets up an unpowered vehicle's default differentials only when it is made.
- **Raw copies stay in the record.** `VehicleRecord` holds `desc` and `written` (the Vehicle as last written, with its input), plus the raw wheels. The raw copies are what an edit is compared against: an edit wakes the chassis even when correcting it gives the same value, as it did before.

One edge case changed: before, the front end compared the lean limit as written, and now Jolt compares it after correction. So a motorcycle whose lean limit goes from one out-of-range value to another that clamps to the same angle is no longer rebuilt. The debug lines read tyres from the description's wheels. `clang-x64-debug` passes 81/81 tests unedited, and `scripts/demo-smoke.sh` passed all nine physics scenes.
