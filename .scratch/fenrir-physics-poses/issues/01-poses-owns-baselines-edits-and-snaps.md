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

**Status:** ready-for-agent

- [ ] Neither `Bodies::Record` nor `CharacterRecord` has a `world` field; no `ReadBack`, `SnapBody` or `SnapCharacter` remains.
- [ ] `Shown` is private to Poses; joint state reads a step pose.
- [ ] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [ ] `scripts/demo-smoke.sh` passes all nine physics scenes.
