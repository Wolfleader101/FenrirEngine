# 04: Distance, slider and swing-twist kinds

**What to build:** Joints can be any of the six kinds (spec: "the joint component and kinds", "the Jolt backend"). `DistanceJoint`, `SliderJoint` and `SwingTwistJoint` join `JointKind`, with every field and default the spec lists, mapped privately to the backend's distance, slider and swing-twist constraints in local space against centre-of-mass frames. Each frame's local X is the slider direction and the swing-twist twist axis; swing is about Y and Z.

- `DistanceJoint { minDistance = 0, maxDistance = 1, spring }` keeps the two frame positions between min and max (a rod at min == max, a rope at min 0) and ignores frame rotations.
- `SliderJoint` slides along X. Its limits, motor and friction behave from ticket 06; here they exist as fields with defaults.
- `SwingTwistJoint` with its swing and twist fields and `SwingTwistMotor`; the cone and twist limits apply as hard limits with their defaults. Limit validation and the motor come in ticket 06.
- The shared settings `JointSpring`, `JointLimits`, `JointMotorMode`, `JointMotor` and `SwingTwistMotor` exist with the spec's defaults.

**Blocked by:** 03 (Joints tracer bullet).

**Status:** resolved

- [x] Tests: each new kind holds its bodies at the rest pose; a slider moves only along X; a distance joint keeps its bodies between min and max (a rod at min == max, a slack rope at min 0); a swing-twist joint stays within its default cone and twist.
- [x] Changing a joint's kind by assignment between all six kinds recreates it without moving the rest pose.
- [x] Every kind and every shared setting has the spec's default (test).

## Answer

Built as specified. Public: `FenrirPhysics/Joints.hpp` gains `JointSpring`, `JointLimits`, `JointMotorMode`, `JointMotor`, `SwingTwistMotor`, `DistanceJoint`, `SliderJoint` and `SwingTwistJoint`, and `JointType` is now `Fixed, Ball, Distance, Hinge, Slider, SwingTwist` (the spec's order; nothing mirrors it yet). `HingeJoint` gains its `limits`, `motor` and `friction` fields too, since the spec gives hinge and slider the same shape. Private: the Jolt backend builds distance, slider and swing-twist constraints in local space against centre-of-mass frames, frame X as the slider and twist axis and frame Y as the normal and plane axis.

- **Swing axes:** Jolt's plane half-cone angle is the swing about the frame's Y (`swingYLimit`), its normal half-cone angle the swing about Z (`swingZLimit`).
- **Applied now:** a distance joint's `minDistance` and `maxDistance`, at creation and as a live edit (`SameLive` compares them; `UpdateJoint` calls `SetDistance`), and the swing-twist cone and twist limits at creation. The backend maps out-of-range values silently, because Jolt asserts on them: a negative min is raised to 0 and a max below min raised to it, cone half-angles are clamped to [0, π], and the twist range to [-π, 0] and [0, π]. Ticket 06 adds the warnings, and the swing-twist limits as live edits.
- **Fields only, marked "Not yet applied":** the distance spring, hinge and slider limits, motors and friction, and the swing-twist friction and motor (ticket 06).
- **Tests** (`Tests/Joints.cpp`): the rest-pose test covers all six kinds (the distance joint as a rod of length 0); slider along a frame X turned onto world Z; a 2 m rod whose frame rotations differ (ignored), which swings and turns while staying within 2 cm of 2 m; a slack 2 m rope the box falls freely on, then hangs from; a swing-twist joint twisted and swung hard about each axis, which stops at π/4 ± 0.05; every ordered pair of the six kinds swapped by assignment without moving the rest pose; a distance range edited live.
- **Checked by running:** clang debug and release, every suite. MSVC x64 debug and release (scripting off), `FenrirPhysicsTests`. No build adds a warning in the code this ticket touched.
- **No hand check:** the ticket changes no scene.
