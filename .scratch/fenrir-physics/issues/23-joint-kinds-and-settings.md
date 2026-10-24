# Which joint kinds are there, and what does each expose?

Type: grilling
Status: resolved
Blocked by: 20, 22

## Question

Which joint kinds does the joints spec have, and which settings does each expose, in backend-neutral terms (ADR-0010 refuses backend tuning settings)? Builds on [What does Jolt offer for constraints?](20-what-jolt-offers-for-constraints.md) and [How are joints modelled in the ECS?](22-joints-in-the-ecs.md).

- **The list:** fixed, point (ball), distance, hinge, slider, cone, swing-twist, six-DOF; whether gear, rack-and-pinion, pulley and path are in, or left addable later.
- **Per kind:** its limits (angles, distances, soft limits), springs (frequency and damping, or stiffness), and motor (off, velocity, position target; max force or torque).
- **Ragdolls:** which kinds and settings the ragdoll spec will need, so it builds on these rather than adding its own.
- **Neutral names and units:** radians or degrees, frequency or stiffness, what a missing limit means.
- **Which settings are field edits applied on the next tick, and which rebuild the backend constraint** (and whether rebuilds are documented as costly, as shape edits are).
- **Solver settings** (graduated from the map's fog): whether per-joint velocity and position step overrides, and priority, are exposed in neutral terms or refused as backend tuning under ADR-0010. In Jolt an override raises the whole island's step count.

Facts from "What does Jolt offer for constraints?": only hinge and swing-twist are ragdoll-drivable; soft limits and motors exist only on some kinds; hinge and slider limits must include 0.

From "How are joints modelled in the ECS?" (ADR-0013): the kinds are the alternatives of the `JointKind` tagged union inside `Joint`. Bodies, frames and `enabled` are shared fields outside it, and the rest pose (zero angle, zero position) is the two frames coinciding. A kind change recreates the backend constraint.

## Answer

No ADR: each call is either additive to change later (kinds, motor modes, spring modes) or already settled by ADR-0010 (refusing solver settings). Glossary: **joint kind** and **rest pose**.

- **Kinds:** six alternatives of `JointKind`: `FixedJoint`, `BallJoint`, `DistanceJoint`, `HingeJoint`, `SliderJoint`, `SwingTwistJoint`, with a `JointType` enum mirroring `ShapeType`.
  - Left out, addable later as new alternatives: cone (swing-twist covers it) and six-DOF (six axes of modes, limits and motors; the backend has no soft rotation limits on it).
  - Gear, rack-and-pinion, pulley and path are out of the joints spec. Each needs a concept the model lacks: gear and rack-and-pinion couple two other joints, pulley has fixed world points, path needs a shared curve handle.
- **Axes:** each frame's local X axis is the hinge axis, the slider direction and the swing-twist twist axis; swing is measured about the frame's Y and Z. Hinge angles are signed by the right-hand rule about X.
- **Units:** SI and radians throughout, matching `RigidBody`: metres, radians, newtons, newton-metres, m/s, rad/s, all measured from the rest pose. No degree fields (C# may add a degrees helper, ticket 28).
- **Springs:** `JointSpring { float frequency; float damping; }`, Hz and damping ratio (1 is critical); frequency 0 means rigid. No stiffness mode (addable as a field).
- **Limits:** `JointLimits { bool enabled = false; float min; float max; JointSpring spring; }`, shared by hinge and slider only. Disabled means free; the spring makes the limit soft.
  - **The rest pose lies inside the limits** (min ≤ 0 ≤ max), as part of the model; physics never re-centres frames to get round it. A limit excluding 0, or with min > max, is widened to include 0 with one warning. min == max is allowed and mapped privately.
  - Hinge limits lie within [-π, π]; a hinge with no limit spins freely.
- **Motors:** `JointMotor { JointMotorMode mode = Off; float targetVelocity; float targetPosition; float maxForce = infinity; JointSpring spring = {2, 1}; }`, modes Off, Velocity, Position. `maxForce` is symmetric, newtons or newton-metres by kind; the spring sets how hard Position mode drives. Position-and-velocity is left out (an additive mode for the ragdoll spec to ask for).
- **Per kind:**
  - `FixedJoint {}` and `BallJoint {}`: no settings.
  - `DistanceJoint { float minDistance = 0, maxDistance = 1; JointSpring spring; }`: keeps the two frame positions between min and max (a rod at min == max, a rope at min 0, a spring when soft); frame rotations ignored; nothing captured at creation. 0 ≤ min ≤ max: a negative min is raised to 0, and a max below min raised to it, each with one warning.
  - `HingeJoint` and `SliderJoint`: `{ JointLimits limits; JointMotor motor; float friction = 0; }`. Friction is a torque or force resisting motion (a door stays where it is left).
  - `SwingTwistJoint { float swingYLimit = π/4, swingZLimit = π/4; float twistMin = -π/4, twistMax = π/4; float friction = 0; SwingTwistMotor motor; }`.
    - Swing limits are cone half-angles in [0, π] (π frees that swing); no pyramid swing type. Twist range in [-π, π] with the rest pose inside, widened with a warning as for `JointLimits`.
    - Limits are hard: no soft swing-twist limits, so no spring field.
    - `SwingTwistMotor { mode; Quat targetRotation; Vec3 targetAngularVelocity; float maxTorque; JointSpring spring; }`, one motor driving swing and twist together. `targetRotation` is `frameB` relative to `frameA` (identity is the rest pose); `targetAngularVelocity` is in `frameA`'s axes. Velocity mode reads only the velocity target, Position mode only the rotation.
- **Out-of-range values:** negative frequencies, damping ratios, forces, torques and frictions are treated as 0, and frequencies are clamped to half `Time::tickRate`, each with one warning per joint. A position target outside an enabled limit is clamped into it silently.
- **Ragdolls:** the ragdoll spec builds on `HingeJoint` and `SwingTwistJoint` with motors in Position mode and adds no joint kinds. Anything more it needs (position-and-velocity, separate swing and twist motor strengths, collision groups) is an additive request in that spec.
- **Live edits and rebuilds:** every per-kind field (limits, springs, friction, motor mode, targets, strengths) is a live edit applied on the next tick. Only changing the kind, a body or a frame recreates the backend constraint (ADR-0013); the spec documents a rebuild as cheap but briefly softer (one step without warm starting), not costly like shape edits. Whether a live edit wakes sleeping bodies is ticket 26's.
- **Solver settings:** per-joint step overrides and priority are refused as backend tuning under ADR-0010, as are the backend's draw size and user data. Physics fixes the solve order privately, in a stable entity order, for determinism. If the ragdoll or chain demos prove unstable, a later ticket may propose a neutral world-level setting, never a per-joint one.
