# 03: A narrow view of bodies

**What to build:** body records become private to Bodies; other modules see a made body through a few read-only facts and its pose through Poses (spec: "Bodies' view").

- **Private:** `Record` and `Records()` (no callers today) leave Bodies' interface. `RecordOf` is replaced by a read-only view of a made body: its backend id, whether it is active, its motion type and whether it is a trigger. Its world transform comes from Poses' baseline (ticket 01). `FindBody` and `ResolveBody` stay for the queries and Contacts.
- **Callers moved:** Joints (`JointBodyOf`, `IsAwake`, frames' scales), joint state, and Vehicles (`WhyNoChassis`, `ChassisBodyOf`, the chassis's pose and scale) use the view and Poses.
- **Characters' `CharacterBody` set:** kept; a comment at its drain says it is drained only to bring the live set up to date, the touched entities it acts on coming from Bodies' `changed`.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited.

**Blocked by:** 02 (Poses writes and restores wheels).

**Touches:** `packages/FenrirPhysics/src/` (`Bodies.*`, `Joints.*`, `JointState.cpp`, `Vehicles.*`, `Characters.cpp`, `Contacts.cpp`, `Queries.cpp`, `WorldImpl.hpp`).

**Status:** resolved

- [x] No module outside Bodies names `Bodies::Record`; `RecordOf` and `Records()` are gone.
- [x] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [x] `scripts/demo-smoke.sh` passes all nine physics scenes.

**Resolved by:** PR (branch claude/physics-poses-03-narrow-bodies), into claude/physics-poses-spec. `Record`, with `Part`, is private to Bodies; it derives from a public `Bodies::Body` holding only the backend id, `Active()`, the motion and `isTrigger`, so `BodyOf` (which replaces `RecordOf`) and `FindBody` hand out a `const Body*` into the record: null-ness and identity are as before, so Joints' `b == a` check still compares bodies. `Records()` is gone. Joints (`BodyOf`, `IsAwake`), joint state, Vehicles (`WhyNoChassis`, `ChassisBodyOf`, `BlendWheels`) and the queries (`IsSleeping`, `Bounds`, through `Impl::Body`) read the view; the frames' and the chassis's pose and scale already came from Poses' baseline (tickets 01 and 02). Characters' drain of its `CharacterBody` set says it is drained only to bring the live set up to date. `clang-x64-debug` passes 81/81 tests unedited, and `scripts/demo-smoke.sh` passed all nine physics scenes.
