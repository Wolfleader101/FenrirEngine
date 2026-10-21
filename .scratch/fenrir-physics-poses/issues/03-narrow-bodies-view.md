# 03: A narrow view of bodies

**What to build:** body records become private to Bodies; other modules see a made body through a few read-only facts and its pose through Poses (spec: "Bodies' view").

- **Private:** `Record` and `Records()` (no callers today) leave Bodies' interface. `RecordOf` is replaced by a read-only view of a made body: its backend id, whether it is active, its motion type and whether it is a trigger. Its world transform comes from Poses' baseline (ticket 01). `FindBody` and `ResolveBody` stay for the queries and Contacts.
- **Callers moved:** Joints (`JointBodyOf`, `IsAwake`, frames' scales), joint state, and Vehicles (`WhyNoChassis`, `ChassisBodyOf`, the chassis's pose and scale) use the view and Poses.
- **Characters' `CharacterBody` set:** kept; a comment at its drain says it is drained only to bring the live set up to date, the touched entities it acts on coming from Bodies' `changed`.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited.

**Blocked by:** 02 (Poses writes and restores wheels).

**Touches:** `packages/FenrirPhysics/src/` (`Bodies.*`, `Joints.*`, `JointState.cpp`, `Vehicles.*`, `Characters.cpp`, `Contacts.cpp`, `Queries.cpp`, `WorldImpl.hpp`).

**Status:** ready-for-agent

- [ ] No module outside Bodies names `Bodies::Record`; `RecordOf` and `Records()` are gone.
- [ ] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [ ] `scripts/demo-smoke.sh` passes all nine physics scenes.
