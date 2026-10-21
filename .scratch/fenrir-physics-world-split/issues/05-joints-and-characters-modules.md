# 05: Joints and Characters modules

**What to build:** joints and character bodies each become their own private module, reading bodies through Bodies' const view and poses through Poses, each drawing its own debug lines (spec: "FenrirPhysics: the modules" (Joints, Characters, DebugCanvas)).

- **`Joints`** (`Joints.cpp` keeps its name, with a new `src/Joints.hpp`; `JointState.cpp` and `JointBreaking.cpp` fold in or stay as further files of the module) owns its `Joint` tracked set, `JointRecord`s, the index from bodies to joints, `Drain`, `DropOn(destroyed bodies)`, `Sync(dt)` with its `Corrector`, measuring loads, breaking, `GetJointState`, sending `JointBroken`, and `Draw(DebugCanvas&) const` (today's `DrawJoints`). Its inactive-joint warning names a character body through the component rule from ticket 02, not character state.
- **`Characters`** (`Characters.cpp` keeps its name, with a new `src/Characters.hpp`) owns its `CharacterBody` tracked set, `CharacterRecord`s, the inactive-character warnings, `Drop(changed)`, `Make`, `PushEdits`, `Move(dt)` with `ApplyShape`, its write-back of `velocity` and `state` (its pose through Poses), resetting idle characters' state, the end-of-move contacts Contacts reads, and `Draw(DebugCanvas&) const` (today's `DrawCharacters`, drawing its shape through the canvas's collider part).
- **`Step`** calls them as the spec's stage table says; the world keeps no joint or character member.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited.

**Blocked by:** 04 (Contacts module).

**Touches:** `packages/FenrirPhysics/src/` (new `Joints.hpp`, `Characters.hpp`; `WorldImpl.hpp`, `PhysicsWorld.cpp`, `Joints.cpp`, `JointState.cpp`, `JointBreaking.cpp`, `Characters.cpp`, `DebugLines.cpp`), `packages/FenrirPhysics/CMakeLists.txt`.

**Status:** resolved

- [x] `WorldImpl.hpp` declares no joint or character record, set or method.
- [x] Joints and Characters read no other module's records except through Bodies' const view and Poses.
- [x] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [x] Smoke runs (`FENRIR_DEMO_KEYS=1`) with no errors: `"Joints (C++)"`, `"Joints (C#)"`, `"Character (C++)"`, `"Character (C#)"`, `"Vehicles (C++)"` (its pedestrian and driver), `"Vehicles (C#)"` (its towing joint).

**Resolved by:** PR #77 (branch claude/physics-world-split-05), into claude/physics-world-split-spec. Joints is `src/Joints.hpp` in `Fenrir::Detail`, with `Joints.cpp` (the module, `Corrector` and `Draw`), `JointState.cpp` (`Measure`, `StateOf`) and `JointBreaking.cpp` (`Break`, `SendBroken`) kept as its files. It reads Bodies' records through `RecordOf` and step poses through `Poses::BodyShown`, both const. Characters is `src/Characters.hpp`/`.cpp` in `Fenrir::Detail`; its write-back is three calls (`HandPoses` before Poses writes, `ReadBack` and `ResetIdle` after), which `Step` now makes itself, so the world's `WriteBack` is gone. Contacts still gets the characters' end-of-move contacts as arguments from the world, now from `Characters::MoveContacts` and `MoveContactsOf`, so no module calls another. Each module's `Draw` replaces the world's `DrawJoints` and `DrawCharacters`, called in the same place among the categories; `DebugLines.cpp` keeps only the category switch and `DrawVehicles` for ticket 06. The joint and character tracked sets are made in the same order as before. `clang-x64-debug` passes 81/81 tests, and `scripts/demo-smoke.sh` passed all nine physics scenes.
