# 04: Powered ragdolls and strength

**What to build:** `Powered` drives a ragdoll towards its animated pose: each tick, for every powered body, its joint's motor is set to `Position` with the child bone's base-pose rotation relative to its parent bone as the target, in the joint's frames, with torque and spring from `Ragdoll::strength` and `RagdollBody::strengthScale`, so a character braces, holds a pose against gravity and recovers from a shove (spec: "the glue each tick and frame" (`Tick` steps 2 and 4), "the description and the builder" (strength); ADR-0054).

- **Modes:** `Powered` on the ragdoll and per body applied as `motionType = Dynamic` and the joint's motor mode `Position`; any other mode sets the motor `Off` (and leaves its other fields).
- **Targets:** the child bone's base-pose rotation relative to its parent bone from `GetBaseLocal` with `PoseKind::Simulation`, or from both bones' `GetBaseModel` where middle slots are empty, re-expressed in the joint's frames and written as `SwingTwistMotor::targetRotation` or `JointMotor::targetPosition`. No target velocity; the root body gets no drive.
- **Strength:** `maxTorque` (or `maxForce`) = `strength × strengthScale × 30 N m` per kilogram of the child body; spring frequency `lerp(1 Hz, 10 Hz, strength)`, damping 1. The raw motor fields stay reachable on the joint entities and are overwritten only while the body is powered.
- **Docs:** on `Powered`, the base pose is whatever animation produces: pause or swap a cycling locomotion clip for an idle or get-up pose when powering.
- **Glossary:** *powered*, *strength*; keep current.

**Blocked by:** 03 (Simulating, partial ragdolls, blending back and the skeleton entity following).

**Touches:** `packages/FenrirRagdoll/`

**Status:** ready-for-agent

- [ ] `FenrirRagdollTests`: with `Powered` each joint's motor is `Position` with the target equal to the base pose's child-relative rotation in joint frames (a hinge's angle for elbows and knees), and `Off` again when the mode changes.
- [ ] Torque and spring follow `strength` and `strengthScale` as specified; a body's `strengthScale = 0` leaves its motor at zero torque.
- [ ] A powered ragdoll standing on a floor at strength 1 holds a static pose (bones within tolerance of the base pose after 120 ticks); at strength 0 it sags like `Simulating`; the root is undriven.
- [ ] A shove (`AddImpulse`) on a powered ragdoll's chest is recovered from within a bounded time at strength 1.
- [ ] Overrides from ticket 03 still write the simulated pose while powered.
- [ ] Two runs are bit-identical over 300 ticks.
