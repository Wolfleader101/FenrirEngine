# 01: Tracked component set in FenrirECS, and physics' watches moved onto it

**What to build:** FenrirECS gains `TrackedComponents`, and FenrirPhysics replaces its nine `ComponentWatch`es, their initial scans and their drain loops with tracked component sets (spec: "FenrirECS: the tracked component set"; glossary: *tracked component set*).

- **`TrackedComponents`** in its own public header in `FenrirECS`, built on `ComponentWatch`, made from an `EntityList` and a `ComponentType` (native or dynamic). It scans the list when made; each drain returns the entities whose component came or went since the last drain, each once, in entity order (a removal and re-add, or a replace, counts), then brings the live set up to date from "alive and has the type now"; the live set is readable as an ordered range and by membership, and changes only at a drain. No per-entity data; move-only; may outlive its list. Doxygen on every declaration, in the glossary's words.
- **`ComponentWatch` unchanged:** its header, contract and tests stay as they are.
- **Physics:** `PhysicsWorld::Impl`'s nine watches (`RigidBody`, `Collider`, `Trigger`, `Joint`, `CharacterBody`, `Vehicle`, `VehicleWheel`, `VehicleDifferential`, `VehicleAntiRollBar`), the eight `ForEachWith` scans in its constructor and the entity sets they fill (`m_rigidBodyEntities`, `m_colliderEntities`, `m_characterEntities`, `m_jointEntities`, `m_vehicleEntities`, `m_wheelEntities`) become tracked sets. The drain loops in `SyncBodies`, `DrainJoints` and `DrainVehicles` read the touched entities from them; what each feature does for a touched entity (rebuild a body, drop a constraint or a vehicle, forget warnings, re-key a link record) is unchanged. The differential and anti-roll-bar link records stay keyed maps, filled from their sets. `Trigger` keeps contributing only touched entities, as today.
- **ADR-0011:** its 2026-10-16 amendment gains that physics drains its watches through tracked component sets.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited.

**Blocked by:** none.

**Touches:** `packages/FenrirECS/` (a new header and source, `CMakeLists.txt`, `Tests/`), `packages/FenrirPhysics/src/` (`WorldImpl.hpp`, `PhysicsWorld.cpp`, `Joints.cpp`, `Vehicles.cpp`), `docs/adr/0011-physics-components-are-plain-data-physics-diffs.md`.

**Status:** resolved

- [x] `FenrirECSTests` cases for `TrackedComponents`: the initial scan; each touched entity once and in entity order; a removal and re-add, and a replace, counting as touched; the live set after adds, removes, entity destruction and clearing; a dynamic component type; two independent sets on one type; a set outliving its list.
- [x] `PhysicsWorld::Impl` has no `ComponentWatch` member and no `ForEachWith` scan.
- [x] `FenrirPhysicsTests`, `FenrirECSTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [x] Smoke runs (`FENRIR_DEMO_KEYS=1`) of every physics scene with no errors: `"Physics (C#)"`, `"Joints (C++)"`, `"Joints (C#)"`, `"Character (C++)"`, `"Character (C#)"`, `"Vehicles (C++)"`, `"Motorcycle (C++)"`, `"Tank (C++)"`, `"Vehicles (C#)"`.

**Resolved by:** PR #72 (branch claude/tracked-component-set), into claude/physics-world-split-spec. `TrackedComponents` is in `packages/FenrirECS/include/FenrirECS/TrackedComponents.hpp` with its `[tracked]` cases in `Tests/TrackedComponentSets.cpp`. It keeps an entity by its last recorded change, which the watch contract makes the same as "alive and has the type now" and which stays safe once the set outlives its list. Physics drains each tracked set once per entity in entity order, where it used to handle every change in recorded order; the joint and vehicle drops already did nothing after the first. `clang-x64-debug` passes 81/81 tests, and the nine smoke runs were clean.
