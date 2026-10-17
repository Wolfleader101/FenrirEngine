# 06: Limits, motors, springs and friction

**What to build:** Joints can be limited, driven and damped, every setting a live edit (spec: "the joint component and kinds", "the Jolt backend"). Units are SI and radians, measured from the rest pose.

- **Limits:** `JointLimits` on hinge and slider; disabled means free, its spring makes it soft. The rest pose lies inside (min ≤ 0 ≤ max): a limit excluding 0, or with min > max, is widened to include 0 with one warning; min == max is allowed. Hinge limits lie within [-π, π]. Swing-twist cone half-angles lie in [0, π] (π frees that swing); the twist range lies in [-π, π] with the rest pose inside, widened with a warning.
- **Motors:** Off, Velocity and Position on hinge and slider (`JointMotor`) and swing-twist (`SwingTwistMotor`). Velocity mode reads only the velocity target, Position mode only the position or rotation target. A position target outside an enabled limit is clamped into it silently.
- **A hard limit always beats a motor,** whatever its max force: once a hinge or slider is at an enabled hard limit, a Velocity motor's push further out is dropped for that step; the same holds for swing-twist against its cone and twist limits. A soft limit may be pushed past.
- **Friction** on hinges, sliders and swing-twist joints; the distance joint's spring.
- **Out-of-range values:** negative frequencies, damping ratios, forces, torques, frictions and break thresholds are treated as 0, and frequencies are clamped to half `Time::tickRate`, each with one warning per joint. A distance joint's negative min is raised to 0 and a max below min raised to it, each with one warning.
- The Joints (C++) door gains its limit, friction and motor, with keys 1 push the door, 2 cycle the door motor (off, +velocity, −velocity, position 0, logging whether it was asleep), L toggle the door's limits, each registered for the smoke run.

**Blocked by:** 04 (Distance, slider and swing-twist kinds).

**Status:** ready-for-agent

- [ ] Tests: hard and soft limits on hinge and slider; a limit excluding the rest pose is widened with one warning; min == max holds.
- [ ] Tests: Velocity and Position motors on hinge, slider and swing-twist reach their targets; a position target outside a limit is clamped; an infinite Velocity motor is held at a hard hinge, slider and swing-twist limit.
- [ ] Tests: friction keeps a hinge where it is left; a distance spring bobs; swing-twist cones and twist hold.
- [ ] Tests: each out-of-range value is corrected with one warning per joint; every limit, motor, spring and friction edit reaches physics on the next tick and wakes a sleeping pair.
- [ ] The door swings, stops at its limits, a velocity motor holds it at its limit, and a motor edit wakes a sleeping door (checked by hand).
