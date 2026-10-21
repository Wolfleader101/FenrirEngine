# 01: One inactive tracker and one way to correct out-of-range values

**What to build:** every feature tracks its inactive components through one helper and corrects out-of-range values through another, keeping every warning's text and when it fires; vehicle correction moves to its own file of the Vehicles module (spec: "The inactive tracker", "Out-of-range values", "Vehicles' files").

- **Inactive tracker** (private, in `src/`): per entity, why it is inactive; warns when the reason appears, clears when valid, forgets entities whose component went. Joints, Characters, Vehicles (vehicles, wheels, differentials, anti-roll bars) and Bodies (`RigidBody`s with no collider, colliders beneath a character) use it; the `inactive` flags on `JointRecord` and `VehicleLinkRecord` and the hand-written sets and loops go.
- **Out-of-range helper** (private, in `src/`, on `WarnOnce`): the warn-once bitmask, "not negative", "clamp into range", and the message prefix. `Corrector`, `WheelCorrector`, the engine/gearbox/track corrections with `InRange`, and the link checks use it, each stating its lifetime: once per component (joints, wheels, links) or once until valid (a vehicle's kind).
- **`VehicleSettings.cpp`** (or the name you choose) holds Vehicles' `Same` overloads, corrections and description building; Vehicles' interface is unchanged.
- Every warning's text, the entities it names, and the tick it fires on are unchanged; behaviour and order unchanged; FenrirPhysics's and the scripting tests pass unedited.

**Blocked by:** none.

**Touches:** `packages/FenrirPhysics/src/` (new tracker and helper headers, new `VehicleSettings.cpp`; `Joints.*`, `Characters.*`, `Vehicles.*`, `Bodies.*`, `WarnOnce.hpp`), `packages/FenrirPhysics/CMakeLists.txt`.

**Status:** ready-for-agent

- [ ] No `inactive` flag on `JointRecord` or `VehicleLinkRecord`, and no hand-written inactive set-and-loop in Characters, Vehicles or Bodies.
- [ ] `Corrector`, `WheelCorrector`, `InRange` and the link checks share one warn-and-correct helper; no copied `Warn`/`NotNegative` remains.
- [ ] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [ ] `scripts/demo-smoke.sh` passes all nine physics scenes.
