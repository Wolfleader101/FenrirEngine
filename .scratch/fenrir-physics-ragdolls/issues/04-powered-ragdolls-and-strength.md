# 04: Powered ragdolls and strength

**What to build:** `Powered` drives a ragdoll towards its animated pose: each tick, for every powered body, its joint's motor is set to `Position` with the child bone's base-pose rotation relative to its parent bone as the target, in the joint's frames, with torque and spring from `Ragdoll::strength` and `RagdollBody::strengthScale`, so a character braces, holds a pose against gravity and recovers from a shove (spec: "the glue each tick and frame" (`Tick` steps 2 and 4), "the description and the builder" (strength); ADR-0054).

- **Modes:** `Powered` on the ragdoll and per body applied as `motionType = Dynamic` and the joint's motor mode `Position`; any other mode sets the motor `Off` (and leaves its other fields).
- **Targets:** the child bone's base-pose rotation relative to its parent bone from `GetBaseLocal` with `PoseKind::Simulation`, or from both bones' `GetBaseModel` where middle slots are empty, re-expressed in the joint's frames and written as `SwingTwistMotor::targetRotation` or `JointMotor::targetPosition`. No target velocity; the root body gets no drive.
- **Strength:** `maxTorque` (or `maxForce`) = `strength × strengthScale × 30 N m` per kilogram of the child body; spring frequency `lerp(1 Hz, 10 Hz, strength)`, damping 1. The raw motor fields stay reachable on the joint entities and are overwritten only while the body is powered.
- **Docs:** on `Powered`, the base pose is whatever animation produces: pause or swap a cycling locomotion clip for an idle or get-up pose when powering.
- **Glossary:** *powered*, *strength*; keep current.

**Blocked by:** 03 (Simulating, partial ragdolls, blending back and the skeleton entity following).

**Touches:** `packages/FenrirRagdoll/`

**Status:** resolved

- [x] `FenrirRagdollTests`: with `Powered` each joint's motor is `Position` with the target equal to the base pose's child-relative rotation in joint frames (a hinge's angle for elbows and knees), and `Off` again when the mode changes.
- [x] Torque and spring follow `strength` and `strengthScale` as specified; a body's `strengthScale = 0` leaves its motor at zero torque.
- [x] A powered ragdoll standing on a floor at strength 1 holds a static pose (bones within tolerance of the base pose after 120 ticks); at strength 0 it sags like `Simulating`; the root is undriven.
- [x] A shove (`AddImpulse`) on a powered ragdoll's chest is recovered from within a bounded time at strength 1.
- [x] Overrides from ticket 03 still write the simulated pose while powered.
- [x] Two runs are bit-identical over 300 ticks.

**Resolved by:** branch `ragdoll/04-powered`. After its body loop, `Tick` visits every joint between two of the
ragdoll's bodies (`bodyA` the parent, `bodyB` the child): `Position` while both resolved and the child is powered,
`Off` otherwise (a skipped powered body's joint included) with the other fields left; joints to anything else, the
root body's included, are untouched. A powered
child's target is the child bone's base-pose rotation relative to its parent bone (`GetBaseLocal` when the parent
part's bone is its skeleton parent, else both `GetBaseModel`s), through both offsets and the joint's frames, as
`targetRotation` or, for a hinge, the turn about X as `targetPosition`; target velocities are written 0; torque
`strength × strengthScale × 30 N m/kg` of the child, spring `lerp(1, 10 Hz, strength)`, damping 1, strength taken
within [0, 1]. Joints are written independently, so no root-first order is needed. Tests:
`packages/FenrirRagdoll/Tests/Powered.cpp` and the powered case in `Determinism.cpp`; `Character`, `Weightless`,
`Decomposed` and `JointTo` moved into `RagdollTest.hpp`.

The holding and shove boxes are tested with the ragdoll lying on its back (arms raised against gravity), and the shove
weightless: standing, the undriven root topples the ragdoll, and the feet's 30 N m/kg × 1.05 kg cannot carry the body's
weight on the foot capsule's toe end (the ankles fold about 40°). A held limb settles about 5° under its weight (a
motor is a spring), so the hold's tolerance is 8°; the shove recovers within 2° in 63 ticks (bound 90). A world-space
upright drive for the root stays listed, not built (ADR-0054).
