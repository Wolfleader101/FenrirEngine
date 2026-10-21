# 03: Poses module: write-back order and interpolation

**What to build:** the Transforms physics writes for bodies and characters, and how they are shown between ticks, move into one private module, Poses (spec: "FenrirPhysics: the modules" (Poses), "the stages of `Step`" (write-back, shown poses)).

- **`Poses`** (`src/Poses.hpp`, `src/Poses.cpp`, in `Fenrir::Detail`) owns `Shown` per entity (out of `Record` and `CharacterRecord`), writing step poses back parents before children (bodies and characters in one order, as `WriteBack` does today), `Advance` (today's `AdvancePoses`), `Restore` (a body's turn is a teleport, a character's is not) and `Blend` (one depth-sorted list over bodies and characters, with the carried-at-step-pose rule for one not interpolated beneath one that is).
- **Bodies** hands Poses each dynamic body's new world pose and keeps writing its `RigidBody` velocities and its record; **Characters** hands Poses each character's new world position and keeps writing its `velocity` and `state`. A teleport marks the entity's snap through Poses. What each side reads back for the diff (`GetWorldTransform` after the write) is unchanged.
- **Wheels** stay with vehicle code: `BlendWheels`, `RestoreWheels` and `AdvanceWheels` read the chassis's `Shown` from Poses, and the vehicles' write-back still runs before `Advance`, which clears the snap they read.
- **Joint state** reads a body's last step pose (`shown.current` today) from Poses.
- The world's `Restore` and `Blend` (called by `Physics.cpp`'s `BeforeTicks` and `AfterTicks` systems) call Poses, then the wheels.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited, the interpolation tests included.

**Blocked by:** 02 (Bodies module, the stages of `Step`, and the debug canvas).

**Touches:** `packages/FenrirPhysics/src/` (new `Poses`; `WorldImpl.hpp`, `PhysicsWorld.cpp`, `Bodies.*`, `Characters.cpp`, `Vehicles.cpp`, `JointState.cpp`), `packages/FenrirPhysics/CMakeLists.txt`.

**Status:** ready-for-agent

- [ ] `Shown` is declared in Poses only; no body or character record holds one.
- [ ] Bodies and character code call no `SetWorldTransform` for a step pose; Poses does.
- [ ] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [ ] Smoke runs (`FENRIR_DEMO_KEYS=1`) of every physics scene with no errors: `"Physics (C#)"`, `"Joints (C++)"`, `"Joints (C#)"`, `"Character (C++)"`, `"Character (C#)"`, `"Vehicles (C++)"`, `"Motorcycle (C++)"`, `"Tank (C++)"`, `"Vehicles (C#)"`.
