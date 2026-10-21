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

**Status:** ready-for-agent

- [ ] `Backend::World` has five vehicle methods; `Backend.hpp` has no `DrivetrainOf` and no `VehicleWheel` inside `VehicleWheelDesc`; `JoltBackend.cpp` does not name `TrailerVehicle`.
- [ ] The front end has no `Rebuilds`; `VehicleRecord` holds the last applied description and input.
- [ ] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [ ] `scripts/demo-smoke.sh` passes all nine physics scenes.
