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

**Status:** resolved

- [x] Vehicles calls no `SetWorldTransform` and reads nothing of Poses but the chassis's blended pose and whether it snapped.
- [x] `Vehicles::RestoreWheels` no longer exists.
- [x] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [x] `scripts/demo-smoke.sh` passes all nine physics scenes.

**Resolved by:** PR #PRNUM (branch claude/physics-poses-wheels), into claude/physics-poses-spec. Vehicles hands each wheel's pose to Poses in the chassis's axes (`Poses::HandWheel`), before the one `Poses::WriteBack`, which writes the bodies and characters, then the wheels against the chassis's read-back step pose, vehicle by vehicle in chassis entity order, parents first, scale left game code's. A new vehicle's wheels, posed at once in the edits as before, go through the same hand and `WriteBack`. At blend time `Vehicles::BlendWheels` still blends compression, steer and unwrapped spin as numbers and hands each wheel's world pose (`Poses::HandBlendedWheel`); it now runs before `Poses::Blend`, which writes the wheels after the bodies, each vehicle's true locals saved before any is written, and `Poses::Restore` puts them back. So the world's `Blend` still calls Vehicles, first rather than after Poses, and its `Restore` no longer does. `Poses::BlendedPose` answers whether `Blend` blends the chassis this frame (interpolated, not held by game code) rather than reading a `blended` flag, so it folds in Vehicles' second `interpolate` check; `Poses::Snaps` answers `AdvanceWheels`. Wheels keep no entry in Poses' map: being output only they hold nothing from one step to the next, so Poses keeps only their handed poses and their true locals until `Restore`, by chassis. Vehicles still reads `Poses::Baseline` for the chassis's scale and its debug lines, as the spec has it. `clang-x64-debug` passes 81/81 tests unedited, and `scripts/demo-smoke.sh` passed all nine physics scenes.
