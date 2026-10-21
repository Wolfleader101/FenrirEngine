# 01: Poses owns bodies' and characters' baselines, edits and snaps

**What to build:** Poses keeps one entry per body and character holding the pose physics last applied or wrote back, answers whether game code moved or turned it, and sets snaps itself; write-back becomes data handed in and one write (spec: "Poses: one entry per entity", "Poses: what it answers", "Poses: writing, advancing, blending, restoring" (bodies and characters)).

- **One entry per entity**, tagged written, followed or still body, or character, holding the baseline (a full world transform, read back after a write), the step pose and how it is shown between ticks. `Shown` and `BodyPose` become private; the twin methods become one each.
- **Edits:** Bodies' `PushEdits` and Characters' `PushEdits` ask Poses whether the entity was moved or turned since physics last put a pose there, instead of diffing their own `world`; Bodies teleport on either (kinematic bodies are moved, as today), Characters teleport on a move and turn on a turn. Poses marks snaps by its `Restore` rule; `SnapBody` and `SnapCharacter` go.
- **Baselines:** `Bodies::Record::world` and `CharacterRecord::world` go. Bodies and Characters set the baseline when they make or push to the backend; Poses sets it at write-back. Everything that read `record.world` (the diff, debug lines, joint frames' and the chassis's scale, wheel posing) reads it from Poses.
- **Write-back:** Bodies and Characters hand their step poses; `Poses::WriteBack` writes them parents first in today's order and keeps the read-back pose as baseline and step pose. `Bodies::ReadBack` and `Characters::ReadBack` go; `Characters::ResetIdle` stays.
- **Joint state** asks Poses for a body's last step pose instead of reading `Shown::current`.
- Vehicles may still read the chassis's `snap`, `blended` and blended pose through a temporary accessor until ticket 02; nothing else outside Poses reads its entries.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited.

**Blocked by:** none.

**Touches:** `packages/FenrirPhysics/src/` (`Poses.*`, `Bodies.*`, `Characters.*`, `JointState.cpp`, `Joints.cpp`, `Vehicles.cpp`, `PhysicsWorld.cpp`, `WorldImpl.hpp`).

**Status:** resolved

- [x] Neither `Bodies::Record` nor `CharacterRecord` has a `world` field; no `ReadBack`, `SnapBody` or `SnapCharacter` remains.
- [x] `Shown` is private to Poses; joint state reads a step pose.
- [x] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [x] `scripts/demo-smoke.sh` passes all nine physics scenes.

**Resolved by:** PR (branch claude/physics-poses-01), into claude/physics-poses-spec. Poses keeps one entry per body and character in one map by entity, tagged with a public `Kind` (`Written`, `Followed`, `Still`, `Character`), holding its baseline and its private `Shown`. Bodies and Characters call `Made` when they make one and `Forget` when its record goes, and their `PushEdits` call `Push`, which judges the world transform against the baseline (`Edit::Moved`, `Turned` or `None`), makes it the new baseline, and sets the snap (a move, or a turn unless a character; never a followed body). `Hand` replaces `HandBody` and `HandCharacter`; `WriteBack` reads each written pose back as the baseline, which is also the step pose `Advance` and `Blend` use. `Bodies::ReadBack` is gone; `Characters::ReadBack`'s velocity and state writes moved into `Characters::HandPoses`, before `Poses::WriteBack` (they touch no Transform), and `ResetIdle` stays. The debug lines, joint frames' and the chassis's scale, and wheel posing read `Poses::Baseline`; joint state reads `Poses::StepPose`. Vehicles reads the chassis's snap and blended pose through `Poses::Snaps` and `Poses::BlendedPose` until ticket 02. `clang-x64-debug` passes 81/81 tests unedited, and `scripts/demo-smoke.sh` passed all nine physics scenes.
