# 02: Poses writes and restores wheels

**What to build:** wheel entities become Poses entries: Vehicles hands Poses each wheel's world pose at write-back and at blend time, and Poses writes, orders and restores them; Vehicles keeps only the wheel math (spec: "Poses: one entry per entity" (wheels), "Poses: what it answers" (a chassis's blended pose, whether it snapped), "Poses: writing, advancing, blending, restoring").

- **Wheel entries**, tagged with their chassis, output only: game code's writes to a wheel's Transform are never edits, and `Restore` puts back its true local Transform whatever game code wrote, as today.
- **Write-back:** after the chassis's pose is written, Vehicles hands each wheel's world pose (the chassis's step pose × `WheelPose`, scale left game code's); Poses writes them parents first, after the bodies and characters, as today.
- **Blend:** Vehicles asks Poses for the chassis's blended pose this frame (none if `Blend` did not blend it: not interpolated, or held by game code) and hands each wheel's blended world pose from its blended compression, steer and unwrapped spin; Poses saves each true local Transform before writing any, writes parents first, and restores them in `Restore`.
- **Snap:** `AdvanceWheels` asks Poses whether the chassis snapped at the last step, instead of reading its `Shown`.
- **Gone from Vehicles:** `trueLocals`, `RestoreWheels`, the `blended` flag, the wheel depth sorts and the second `interpolate` check, and ticket 01's temporary accessor. Vehicles keeps `WheelShown`, `AdvanceWheels` and `WheelPose`.
- `Step`, `Restore` and `Blend` keep their stages; the world's `Restore` and `Blend` no longer call Vehicles.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited, the vehicle pose and interpolation tests included.

**Blocked by:** 01 (Poses owns bodies' and characters' baselines, edits and snaps).

**Touches:** `packages/FenrirPhysics/src/` (`Poses.*`, `Vehicles.*`, `PhysicsWorld.cpp`, `WorldImpl.hpp`).

**Status:** ready-for-agent

- [ ] Vehicles calls no `SetWorldTransform` and reads nothing of Poses but the chassis's blended pose and whether it snapped.
- [ ] `Vehicles::RestoreWheels` no longer exists.
- [ ] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [ ] `scripts/demo-smoke.sh` passes all nine physics scenes.
