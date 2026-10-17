# 04: Distance, slider and swing-twist kinds

**What to build:** Joints can be any of the six kinds (spec: "the joint component and kinds", "the Jolt backend"). `DistanceJoint`, `SliderJoint` and `SwingTwistJoint` join `JointKind`, with every field and default the spec lists, mapped privately to the backend's distance, slider and swing-twist constraints in local space against centre-of-mass frames. Each frame's local X is the slider direction and the swing-twist twist axis; swing is about Y and Z.

- `DistanceJoint { minDistance = 0, maxDistance = 1, spring }` keeps the two frame positions between min and max (a rod at min == max, a rope at min 0) and ignores frame rotations.
- `SliderJoint` slides along X. Its limits, motor and friction behave from ticket 06; here they exist as fields with defaults.
- `SwingTwistJoint` with its swing and twist fields and `SwingTwistMotor`; the cone and twist limits apply as hard limits with their defaults. Limit validation and the motor come in ticket 06.
- The shared settings `JointSpring`, `JointLimits`, `JointMotorMode`, `JointMotor` and `SwingTwistMotor` exist with the spec's defaults.

**Blocked by:** 03 (Joints tracer bullet).

**Status:** ready-for-agent

- [ ] Tests: each new kind holds its bodies at the rest pose; a slider moves only along X; a distance joint keeps its bodies between min and max (a rod at min == max, a slack rope at min 0); a swing-twist joint stays within its default cone and twist.
- [ ] Changing a joint's kind by assignment between all six kinds recreates it without moving the rest pose.
- [ ] Every kind and every shared setting has the spec's default (test).
