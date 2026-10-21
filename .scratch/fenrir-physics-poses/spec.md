Status: ready-for-agent

# FenrirPhysics poses: one owner for every Transform physics writes, and a narrow view of bodies

Origin: the second architecture review of FenrirPhysics (2026-10-21), after the world split, candidates A ("deepen Poses: one owner for every pose physics writes") and B ("make Bodies' view as narrow as the spec said"), grilled to the decisions below. Like the world split it builds on (PRs #72, #73, #75–#78), it is a refactor: it changes no behaviour, no warning and no order, and no public interface of FenrirPhysics. ADR-0010, ADR-0011, ADR-0014 and ADR-0015 apply, and none is reopened. It does reopen one decision of the world split's spec: that wheels' poses, blending and restoring stay with vehicle code.

## Problem Statement

The world split gave FenrirPhysics private modules (Bodies, Characters, Joints, Vehicles, Poses, Contacts) called by named stages in `Step`. Two of its seams came out shallow.

**Poses.** It is meant to own the Transforms physics writes, but:
- Its interface is four body/character twin pairs (`HandBody`/`HandCharacter`, `SnapBody`/`SnapCharacter`, `ForgetBody`/`ForgetCharacter`, `BodyStepPose`/`CharacterStepPose`) plus a public `Shown` record that Vehicles and joint state read (`snap`, `blended`, `Between`, `current`).
- Writing step poses back takes seven calls across four modules (`Bodies::HandPoses`, `Characters::HandPoses`, `Poses::WriteBack`, `Bodies::ReadBack`, `Characters::ReadBack`, `Characters::ResetIdle`, `Vehicles::WriteBack`), and `ReadBack` only copies Poses' step pose back into the record.
- One pose is stored three times: `Bodies::Record::world` (or `CharacterRecord::world`), Poses' `Entry::step`, and `Shown::current`.
- Teleports are detected twice: Bodies and Characters each diff the entity's world transform against their own `world`, then tell Poses to snap; Poses' `Restore` has its own rule for what game code changed.
- Vehicles keeps a parallel copy of interpolation for wheels: its own true local Transforms, `RestoreWheels`, a `blended` flag, a second parents-first depth sort and a second check of the chassis's `interpolate`. It depends on an order written only in comments: its write-back after Bodies' `ReadBack`, before `Poses::Advance` clears the chassis's `snap`, and its blend after `Poses::Blend` sets `blended`.
- Joint state reads `Shown::current`, which is the last step pose only because `Advance` has already run.

**Bodies' view.** The split agreed that other modules read bodies through a narrow const view. In fact `RecordOf` hands out the whole `Record` (parts, material, filter, velocities and the rest) at eight call sites in Vehicles, Joints and joint state, which read only its backend id, whether it is active, its motion type, whether it is a trigger, and its world transform. `Records()` has no callers.

## Solution

**Poses becomes the physics side of every Transform physics writes**, for bodies, characters and wheels. It keeps one entry per entity, tagged with what it is, holding the pose physics last applied or wrote back (the diff's baseline) and the last two step poses. It writes poses back parents first, blends and restores them, answers whether game code moved or turned an entity since physics last put a pose there, and sets snaps itself. Bodies and Characters keep no `world` of their own; nothing outside Poses reads `Shown`.

**Vehicles keeps the wheel math and hands Poses the results.** A wheel's local pose comes from its compression, steer and spin in the chassis's axes, and between ticks those are blended as numbers, the spin unwrapped; blending the local rotation would take the short way round. So Vehicles still computes each wheel's world pose, at write-back and at blend time, and Poses writes, orders and restores it. Vehicles asks Poses only for the chassis's blended pose this frame, if it blended one, and whether the chassis snapped this step.

**Bodies' records become private.** Other modules see a made body through a few read-only facts, and its pose through Poses.

## User Stories

1. As an engine maintainer, I want "what physics last put in this Transform" kept in one place, so that a teleport, an edit or an interpolation bug is found in one module.
2. As an engine maintainer, I want writing poses back to be data handed to Poses and one write, so that `Step` does not sandwich a write between hand-offs and read-backs.
3. As an engine maintainer, I want wheels written, ordered and restored by the same module as bodies, so that vehicle code holds only what is particular to wheels.
4. As an engine maintainer, I want other modules to see only the facts of a body they need, so that a new dependency on a body's private state shows at the seam.
5. As an engine maintainer, I want the refactor proven by the existing tests passing unedited, so that I trust it changed nothing a caller can see.

## Implementation Decisions

### Poses: one entry per entity

- One entry per entity, by entity, tagged with what it is:
  - a **written** body (dynamic: its step pose comes from the step and is written to its Transform);
  - a **followed** body (kinematic, or a trigger: game code places it, and it is shown between ticks);
  - a **still** body (static, or inactive: never shown between ticks);
  - a **character** (position only: its rotation and scale stay game code's; a turn is not a teleport);
  - a **wheel**, with its chassis (output only: game code's writes to it are not edits).
- Each holds the **baseline**: the world transform physics last applied or wrote back, read back the way the diff reads (`GetWorldTransform` after the write, so rounding through a parent is not an edit). Bodies and Characters set it when they make or push to the backend, and Poses sets it when it writes back. It is a full Transform: joints and vehicles read a body's world scale from it.
- Each holds how it is shown between ticks (today's `Shown`), private to Poses.
- `Shown` and `BodyPose` stop being public; the body/character twin methods become one each, taking the entity (and, where it matters, the kind).

### Poses: what it answers

- **Edits.** For an entity, whether game code moved it, turned it, or neither since physics last put a pose there, judged against the baseline (a moved parent counts). Bodies take either as a teleport (kinematic bodies are moved, as today); Characters take a move as a teleport and a turn as a turn. Poses marks the snap by the same rule its `Restore` already uses (a body's turn snaps, a character's does not), so `SnapBody` and `SnapCharacter` go.
- **The last step pose** of a body, for joint state, replacing its read of `Shown::current`.
- **The baseline** of a body or character, for the diff, for joint frames' and wheel positions' scale, and for debug lines.
- **A chassis's blended pose** this frame, if `Blend` blended it (interpolated and not held by game code), and **whether the chassis snapped** at the last step, for Vehicles.

### Poses: writing, advancing, blending, restoring

- **Write-back:** Bodies hand each body's step pose and kind, Characters each character's position, and Vehicles each wheel's world pose (after the chassis's, against its step pose); Poses writes them parents first, bodies, characters and wheels in one order as today's orders give, reads each written pose back as the baseline, and keeps it as the step pose. `Bodies::ReadBack` and `Characters::ReadBack` go; Characters' `ResetIdle` stays Characters' (it writes `state`, not a pose).
- **Advance, Restore and Blend** cover wheels too. At blend time Vehicles hands each wheel's blended world pose; Poses saves its true local Transform and writes it, and `Restore` puts it back (a wheel's Transform is output only: whatever game code wrote since goes, as today). Vehicles' `trueLocals`, `RestoreWheels`, `blended` flag, wheel depth sort and second `interpolate` check go. Vehicles keeps its `WheelShown` numbers (compression, steer, unwrapped spin), `AdvanceWheels` and `WheelPose`.
- **Forget:** a module tells Poses when an entity's record goes, so a new body, character or wheel on it starts afresh.
- **Order kept:** the stages in `Step` stay where they are; within write-back, bodies' and characters' poses are written in today's one parents-first order, and wheels after them, as today.

### Bodies' view

- `Record` and `Records()` become private; `RecordOf` is replaced by a read-only view of a made body: its backend id, whether it is active, its motion type and whether it is a trigger. Its world transform comes from Poses' baseline. `FindBody` and `ResolveBody` stay for the queries and Contacts.
- Joints (`JointBodyOf`, `IsAwake`, frames' scales, joint state) and Vehicles (`WhyNoChassis`, `ChassisBodyOf`, the chassis's pose and scale, `BlendWheels`) move onto it and onto Poses.
- Characters keeps its own `CharacterBody` tracked set, which it drains only to bring the live set up to date; a comment at the drain says so (the touched entities it acts on come from Bodies' `changed`, which also covers `Collider` and `RigidBody`).
- `PhysicsWorld`'s `friend` access to the world's members stays: the world is its pimpl.

## Testing Decisions

- The public `PhysicsWorld` tests in `FenrirPhysicsTests` (the interpolation, teleport, character pose and vehicle pose tests among them) and the physics scripting tests pin behaviour, messages, ordering and determinism. Every ticket leaves them passing **unedited**.
- No new tests: the modules are private (ADR-0010).
- Verification per ticket, per the README: build and test `clang-x64-debug`, and run `scripts/demo-smoke.sh` over the nine physics scenes.

## Out of Scope

- The vehicle backend seam, the inactive-and-warned-once pattern, value correction, component descriptions, event scoping, DemoScenes and the scripting extension (the review's candidates C–I).
- Any change to the public FenrirPhysics interface, `Physics.cpp`'s systems, the scripting extension or the DemoApp.
- Blending wheels as poses: Vehicles keeps blending their numbers.

## Further Notes

- **Delivery:** three tickets in `issues/`, one stack (all touch `packages/FenrirPhysics/src/`). If PR #71 (the world split into `dotnet-scripting`) has merged, the first PR targets `dotnet-scripting`; otherwise it stacks on `claude/physics-world-split-spec`, and `claude/physics-poses-spec` (this plan) is its base.
- These do not hold PR #69.
- No ADR: private, reversible, no behaviour. No glossary change: Poses, baseline and view are architecture words, not domain ones.
- When ticket 03 merges, the commit that records the spec built deletes `.scratch/fenrir-physics-poses/`.
