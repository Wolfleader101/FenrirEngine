# 02: Bodies module, the stages of `Step`, and the debug canvas

**What to build:** Bodies becomes its own private module at the bottom of the world, `Step` calls the stages by name with the body rebuild split in two, and the shared helpers and the debug canvas get their own headers (spec: "FenrirPhysics: the modules" (Bodies, DebugCanvas, Queries, shared helpers), "FenrirPhysics: the stages of `Step`").

- **`Bodies`** (`src/Bodies.hpp`, `src/Bodies.cpp`, in `Fenrir::Detail`) owns the `RigidBody`, `Collider`, `Trigger` and its own `CharacterBody` tracked sets, `Record` and `Part`, the empty-`RigidBody` and character-child warnings, the push queue, `PartsOf`/`UsableParts`/`WarnAbout`, `EffectiveFilterOf`, `MaterialOf`, and the rules for which colliders make which body (`BodyEntityOf`, `ResolveBody`, `AttachedBodyOf`, character colliders and colliders beneath a character, as functions over components).
- **The rebuild in two calls:** `Plan` (drain, assemble, name the bodies to destroy and the entities whose body components changed) and `Apply` (destroy, then make in one batch in entity order). `Step` calls the other features' drop hooks between them, in today's order: joints' and vehicles' drains, their removals on the destroyed bodies, characters' drop; then `Apply`, then making characters. `SyncBodies` no longer calls any other feature.
- **Bodies' const view** gives what the other features and the queries read: whether an entity is a made body entity, its backend id, world transform, motion and activity; `FindBody` and `ResolveBody`. Joints (`JointBodyOf`, `IsAwake`, the frames' scales), Vehicles (`WhyNoChassis`, `ChassisBodyOf`, the chassis's world transform) and `Queries.cpp` move onto it. They still live on the world after this ticket.
- **`Step`** reads as the spec's stage table, with the parts not yet moved still called on the world; the numbered-step comments go.
- **Shared helpers** in private headers: one `ParentOf`, one `DepthOf`, one `FilterOf`, the body-host ancestor test, and a warn-once bitmask helper (adopted by `Corrector`, `WheelCorrector` and the vehicle link warnings).
- **`DebugCanvas`** (`src/DebugCanvas.hpp`, `.cpp`): the line list, the colours and line helpers, the shape-edge cache and drawing a collider part. Bodies gains `Draw(DebugCanvas&) const` for colliders, velocities and bounds; the joint, character, vehicle and contact drawing stays in `DebugLines.cpp` for now, drawing through the canvas.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited.

**Blocked by:** 01 (Tracked component set).

**Touches:** `packages/FenrirPhysics/src/` (new `Bodies`, `DebugCanvas` and helper headers; `WorldImpl.hpp`, `PhysicsWorld.cpp`, `Queries.cpp`, `DebugLines.cpp`, `Joints.cpp`, `JointState.cpp`, `Characters.cpp`, `Vehicles.cpp`), `packages/FenrirPhysics/CMakeLists.txt`.

**Status:** resolved

- [x] No module but the world calls `RemoveJointsOf`, `RemoveVehiclesOf`, `DrainJoints`, `DrainVehicles`, `DropCharacters` or `MakeCharacters`; Bodies' code reads no joint, character or vehicle record.
- [x] Joints, Vehicles and the queries read bodies only through Bodies' const view.
- [x] One definition each of `ParentOf`, `DepthOf` and `FilterOf` in `packages/FenrirPhysics/src/`.
- [x] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [x] Smoke runs (`FENRIR_DEMO_KEYS=1`) of every physics scene with no errors: `"Physics (C#)"`, `"Joints (C++)"`, `"Joints (C#)"`, `"Character (C++)"`, `"Character (C#)"`, `"Vehicles (C++)"`, `"Motorcycle (C++)"`, `"Tank (C++)"`, `"Vehicles (C#)"`.

**Resolved by:** PR #73 (branch claude/physics-world-split-02), into claude/physics-world-split-spec. Bodies is `src/Bodies.hpp`/`.cpp` in `Fenrir::Detail`. The world reaches it through `BodyView()` (const) for joints, vehicles, contacts and the queries, and through `PoseRecords()` for `Restore`, `Blend` and advancing the shown poses until Poses owns `Shown` (now `src/Shown.hpp`). Bodies draws with two calls, `DrawColliders` and `DrawMotion` (velocities and bounds), because the colliders come first and velocities and bounds last, so the line order is unchanged. The world's own `CharacterBody` set is drained at the top of `DropCharacters`; its touched entities arrive in `changed` from Bodies' set. The joint and vehicle drains moved from inside `SyncBodies` to just after `Plan`. Neither reads body records or logs, so the order of backend calls and warnings is the same. The helpers are `Hierarchy.hpp` (`ParentOf`, `DepthOf`, `IsBodyHost`), `FilterOf.hpp` and `WarnOnce.hpp`. `clang-x64-debug` passes 81/81 tests, and the nine smoke runs pressed all their keys with no errors.
