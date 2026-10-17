# 06: Limits, motors, springs and friction

**What to build:** Joints can be limited, driven and damped, every setting a live edit (spec: "the joint component and kinds", "the Jolt backend"). Units are SI and radians, measured from the rest pose.

- **Limits:** `JointLimits` on hinge and slider; disabled means free, its spring makes it soft. The rest pose lies inside (min ≤ 0 ≤ max): a limit excluding 0, or with min > max, is widened to include 0 with one warning; min == max is allowed. Hinge limits lie within [-π, π]. Swing-twist cone half-angles lie in [0, π] (π frees that swing); the twist range lies in [-π, π] with the rest pose inside, widened with a warning.
- **Motors:** Off, Velocity and Position on hinge and slider (`JointMotor`) and swing-twist (`SwingTwistMotor`). Velocity mode reads only the velocity target, Position mode only the position or rotation target. A position target outside an enabled limit is clamped into it silently.
- **A hard limit always beats a motor,** whatever its max force: once a hinge or slider is at an enabled hard limit, a Velocity motor's push further out is dropped for that step; the same holds for swing-twist against its cone and twist limits. A soft limit may be pushed past.
- **Friction** on hinges, sliders and swing-twist joints; the distance joint's spring.
- **Out-of-range values:** negative frequencies, damping ratios, forces, torques, frictions and break thresholds are treated as 0, and frequencies are clamped to half `Time::tickRate`, each with one warning per joint. A distance joint's negative min is raised to 0 and a max below min raised to it, each with one warning.
- The Joints (C++) door gains its limit, friction and motor, with keys 1 push the door, 2 cycle the door motor (off, +velocity, −velocity, position 0, logging whether it was asleep), L toggle the door's limits, each registered for the smoke run.

**Blocked by:** 04 (Distance, slider and swing-twist kinds).

**Status:** resolved, but for the door hand check, which is the user's

- [x] Tests: hard and soft limits on hinge and slider; a limit excluding the rest pose is widened with one warning; min == max holds.
- [x] Tests: Velocity and Position motors on hinge, slider and swing-twist reach their targets; a position target outside a limit is clamped; an infinite Velocity motor is held at a hard hinge, slider and swing-twist limit.
- [x] Tests: friction keeps a hinge where it is left; a distance spring bobs; swing-twist cones and twist hold.
- [x] Tests: each out-of-range value is corrected with one warning per joint; every limit, motor, spring and friction edit reaches physics on the next tick and wakes a sleeping pair.
- [ ] The door swings, stops at its limits, a velocity motor holds it at its limit, and a motor edit wakes a sleeping door (checked by hand).

Notes from building it:

- The out-of-range rules, the rest-pose widening and the position-target clamp are applied in `src/Joints.cpp` as physics applies a joint, so the backend only ever sees settings in range. Each kind of mistake is warned about once per joint, for as long as the joint keeps its `Joint`. Hinge limits beyond a half turn and swing limits outside [0, pi] are also corrected with a warning.
- A hard limit beats a Velocity motor by cutting the motor's target before each step, so that one step at it reaches the limit and goes no further (`CutMotorsAtLimits` in the Jolt backend). For swing-twist, the push out of the cone and the twist past its range are cut.
- Ticket 04 had swing-twist's Y and Z swapped in Jolt: its constraint space is (twist, plane x twist, plane), so the plane axis on frame Y made `swingYLimit` limit the swing about Z. The plane axis is now frame Z, which makes Jolt's constraint space the frames' own axes. Equal default cones hid it; the cone test now uses unequal ones.
