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

**Status:** resolved

- [x] `Shown` is declared in Poses only; no body or character record holds one.
- [x] Bodies and character code call no `SetWorldTransform` for a step pose; Poses does.
- [x] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [x] Smoke runs (`FENRIR_DEMO_KEYS=1`) of every physics scene with no errors: `"Physics (C#)"`, `"Joints (C++)"`, `"Joints (C#)"`, `"Character (C++)"`, `"Character (C#)"`, `"Vehicles (C++)"`, `"Motorcycle (C++)"`, `"Tank (C++)"`, `"Vehicles (C#)"`.
**Resolved by:** PR #75 (branch claude/physics-world-split-03-poses), into claude/physics-world-split-spec. Poses is `src/Poses.hpp`/`.cpp` in `Fenrir::Detail`, and `Shown` is nested in it. Poses reads neither Bodies nor the character records, which keeps dependencies one way. Instead, each step every body hands it its step pose with a `BodyPose` (Written for a dynamic body, Followed for a kinematic body or trigger, Still for a static or inactive one), and every character hands it its position. `Poses::WriteBack` writes them parents first and reads each written pose back, and Bodies (`ReadBack`) and the characters copy that into their records for the diff. Poses keeps the step pose that Advance, Restore and Blend used to read from `record.world`; nothing changes the records between the write-back and the next Step, so the values are the same. Records call `ForgetBody`/`ForgetCharacter` when they go, and a teleport calls `SnapBody`/`SnapCharacter`. `BodyShown` gives Vehicles and JointState a body's `Shown`, or a fresh one if it has no entry yet, as a new record had. The only change in order is that bodies' backend `GetState` reads run in entity order instead of depth order; they are side-effect-free. `clang-x64-debug` passes 81/81 tests, and `scripts/demo-smoke.sh` passed all nine physics scenes.
