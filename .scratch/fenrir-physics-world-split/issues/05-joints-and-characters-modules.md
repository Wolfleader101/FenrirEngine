# 05: Joints and Characters modules

**What to build:** joints and character bodies each become their own private module, reading bodies through Bodies' const view and poses through Poses, each drawing its own debug lines (spec: "FenrirPhysics: the modules" (Joints, Characters, DebugCanvas)).

- **`Joints`** (`Joints.cpp` keeps its name, with a new `src/Joints.hpp`; `JointState.cpp` and `JointBreaking.cpp` fold in or stay as further files of the module) owns its `Joint` tracked set, `JointRecord`s, the index from bodies to joints, `Drain`, `DropOn(destroyed bodies)`, `Sync(dt)` with its `Corrector`, measuring loads, breaking, `GetJointState`, sending `JointBroken`, and `Draw(DebugCanvas&) const` (today's `DrawJoints`). Its inactive-joint warning names a character body through the component rule from ticket 02, not character state.
- **`Characters`** (`Characters.cpp` keeps its name, with a new `src/Characters.hpp`) owns its `CharacterBody` tracked set, `CharacterRecord`s, the inactive-character warnings, `Drop(changed)`, `Make`, `PushEdits`, `Move(dt)` with `ApplyShape`, its write-back of `velocity` and `state` (its pose through Poses), resetting idle characters' state, the end-of-move contacts Contacts reads, and `Draw(DebugCanvas&) const` (today's `DrawCharacters`, drawing its shape through the canvas's collider part).
- **`Step`** calls them as the spec's stage table says; the world keeps no joint or character member.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited.

**Blocked by:** 04 (Contacts module).

**Touches:** `packages/FenrirPhysics/src/` (new `Joints.hpp`, `Characters.hpp`; `WorldImpl.hpp`, `PhysicsWorld.cpp`, `Joints.cpp`, `JointState.cpp`, `JointBreaking.cpp`, `Characters.cpp`, `DebugLines.cpp`), `packages/FenrirPhysics/CMakeLists.txt`.

**Status:** ready-for-agent

- [ ] `WorldImpl.hpp` declares no joint or character record, set or method.
- [ ] Joints and Characters read no other module's records except through Bodies' const view and Poses.
- [ ] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [ ] Smoke runs (`FENRIR_DEMO_KEYS=1`) with no errors: `"Joints (C++)"`, `"Joints (C#)"`, `"Character (C++)"`, `"Character (C#)"`, `"Vehicles (C++)"` (its pedestrian and driver), `"Vehicles (C#)"` (its towing joint).
