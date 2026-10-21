# 06: Vehicles module, and `WorldImpl.hpp` as the world

**What to build:** vehicles become their own private module, `DebugLines.cpp` dissolves into the modules and the canvas, and what is left of `PhysicsWorld::Impl` is the world: the backend, the modules and `Step` (spec: "FenrirPhysics: the modules" (Vehicles, DebugCanvas), "the stages of `Step`").

- **`Vehicles`** (`Vehicles.cpp` keeps its name, with a new `src/Vehicles.hpp`) owns its `Vehicle`, `VehicleWheel`, `VehicleDifferential` and `VehicleAntiRollBar` tracked sets, `VehicleRecord`s and `VehicleLinkRecord`s, the wheel warnings, `Drain`, `DropOn(destroyed bodies)`, `Sync`, `WriteBack(dt)`, its wheels' poses, `AdvanceWheels`, `BlendWheels` and `RestoreWheels`, and `Draw(DebugCanvas&) const` (today's `DrawVehicles`). It reads the chassis through Bodies' const view and the chassis's `Shown` through Poses. Its seven backend calls are unchanged (the vehicle seam is out of scope).
- **`DebugLines.cpp`** is gone: each category draws from its module, the contact category from Contacts (or the backend's touches), and the world switches categories and calls the modules in today's order. `PhysicsWorld`'s debug-line methods read the canvas.
- **`WorldImpl.hpp`** holds the backend, the modules, `Step`, `Restore`, `Blend` and what `PhysicsWorld`'s public methods need, and nothing feature-specific. `PhysicsWorldAccess` is unchanged.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited, the vehicle tests included.
- When this ticket merges, the commit that records the spec built deletes `.scratch/fenrir-physics-world-split/`.

**Blocked by:** 05 (Joints and Characters modules).

**Touches:** `packages/FenrirPhysics/src/` (new `Vehicles.hpp`; `WorldImpl.hpp`, `PhysicsWorld.cpp`, `Vehicles.cpp`, `DebugLines.cpp` removed, `DebugCanvas.*`, `Contacts.*`), `packages/FenrirPhysics/CMakeLists.txt`.

**Status:** ready-for-agent

- [ ] `WorldImpl.hpp` declares no vehicle, wheel or link record, set or method, and no `ComponentWatch` or tracked set.
- [ ] `src/DebugLines.cpp` no longer exists.
- [ ] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [ ] Smoke runs (`FENRIR_DEMO_KEYS=1`) of every physics scene with no errors: `"Physics (C#)"`, `"Joints (C++)"`, `"Joints (C#)"`, `"Character (C++)"`, `"Character (C#)"`, `"Vehicles (C++)"`, `"Motorcycle (C++)"`, `"Tank (C++)"`, `"Vehicles (C#)"`.
