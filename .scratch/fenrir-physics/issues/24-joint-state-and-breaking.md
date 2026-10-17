# What joint state can game code read, and how do joints break?

Type: grilling
Status: resolved
Blocked by: 20, 22

## Question

What can game code read back from a joint, and how does a joint break? Builds on [What does Jolt offer for constraints?](20-what-jolt-offers-for-constraints.md) and [How are joints modelled in the ECS?](22-joints-in-the-ecs.md).

- **State read back:** the current angle or position along the joint's free axes (a door's opening), the force and torque applied last step; written back into the component each step (like a dynamic body's velocity), or asked for through `PhysicsWorld` queries.
- **Breaking:** a force and a torque threshold on the joint; compared with what, over how many steps.
- **A broken joint:** a field state (`broken`, or `enabled` set false by physics), or the component removed (a structural change physics would make); whether it can be repaired.
- **Hearing about it:** a `JointBroken` engine event (plain data naming the joint and both bodies, sorted and sent after the step like contact events), readable through `EventReader<T>`.
- **Determinism and order:** where in the `PostTick` sequence breaking is checked and events are sent.

Facts from "What does Jolt offer for constraints?": breaking is not built in (lambda against a threshold after the step); current values exist only for hinge, slider, pulley, path, and swing-twist and six-DOF rotation; lambda is stale while the joint sleeps.

From "How are joints modelled in the ECS?" (ADR-0013): `Joint` already has `bool enabled = true`, a candidate for what breaking sets; thresholds are expected as shared fields beside it. The joint entity is what an event would name as "the joint".

## Answer

No ADR: both calls that matter can be reversed by adding things (a `broken` field, an opt-in state component), and neither undoes ADR-0011, 0012 or 0013. Glossary: **breaking (a joint)**, covering break force, break torque and a broken joint.

- **Reading state is a query, not a write-back:** `PhysicsWorld::JointState(Entity joint) -> JointState`, reading the last completed step, like `IsSleeping` and `Bounds`.
  - Rejected: writing into `Joint` (physics would write into ECS-owned configuration, ADR-0013) and an opt-in component filled every step (an extra component per joint, computed whether read or not).
  - Nothing is computed until asked; safe from parallel systems; one native function for C#.
  - An entity that is not a joint, or one with no backend joint yet, gives a zeroed state.
- **What it holds:**

  ```cpp
  struct JointState
  {
      Vec3 force;          // N, world space, applied to bodyB (bodyA gets the negative)
      Vec3 torque;         // N·m, likewise
      JointFrame relative; // frameB's pose in frameA's axes; identity is the rest pose
      float position;      // hinge angle (rad, wrapped to (-π, π]), slider offset along X (m), distance length (m); 0 otherwise
  };
  ```

  - Physics computes every reading itself from the bodies' poses and the frames, for every kind, rather than using the backend's per-kind readings (which exist only for hinge, slider and swing-twist rotation).
  - Force and torque sum every part of the joint (position, rotation, limits, motor), privately converted from the backend's per-part impulses by dividing by the step time.
  - Swing-twist's `relative.rotation` is in the same terms as its motor's `targetRotation`. A hinge angle carries no turn count.
- **Which poses:** every reading comes from the last completed step's simulated poses, never from interpolated `Transform`s, so the readings agree with each other and with breaking.
- **Sleep and disabling:** while asleep, force and torque keep the last value they had awake (a sleeping chain still carries its load; sleep stays hidden, as for contacts). While disabled they are 0. Pose readings are always current.
- **Thresholds:** shared fields on `Joint`, `float breakForce = infinity; float breakTorque = infinity;` (N, N·m). Infinity never breaks. Negative values are treated as 0 with one warning per joint, as ticket 23 does.
  - Compared against the magnitudes of `JointState`'s `force` and `torque`, every part included, so a motor straining against a wall can break its hinge.
  - One step over breaks it. No averaging over steps: that is a tuning knob, and game code can average `JointState` itself.
  - Only awake, enabled joints with a finite threshold are checked; a sleeping joint takes no new load.
- **A broken joint is a disabled one** (`enabled = false`, written by physics into the component and its private copy so it is not mistaken for a game edit).
  - Rejected: removing `Joint` (Unity; structural, loses configuration, no repair) and a separate `bool broken` (Unreal; two off switches). The event, not the component, tells a break from a game disable.
  - **Repair** is setting `enabled = true`. No cooldown or grace step: a joint still overloaded breaks again on the next step, with another event, as often as game code re-enables it. A repaired joint's first step lacks warm starting (ticket 23) and is not treated specially. Game code wanting a grace period raises the threshold for a while.
- **Event:** `JointBroken { Entity joint; Entity bodyA; Entity bodyB; Vec3 force; Vec3 torque; }`, with the force and torque that broke it; `bodyB` empty for the world. Read through `EventReader<JointBroken>` in C++ and C#. Sent only for threshold breaks, never for game disables. No opt-in tag: only joints with finite thresholds break.
- **Order in `PostTick`:** a new step **5b, break overloaded joints**, between write-back (5) and events (6). For awake, enabled joints with finite thresholds, compute force and torque; in a stable entity order, disable those over the threshold in the backend, write `enabled = false` with the private copy, and queue `JointBroken`. Events are sorted by joint entity and sent in step 6 with contact and trigger events, once per step. Both bodies were awake for the check, so the release takes effect on the next step with no explicit wake.
- **Left to other tickets:** whether re-enabling a joint, or lowering a sleeping joint's threshold, wakes its bodies (26); the C# surface (28); demo content (29).
