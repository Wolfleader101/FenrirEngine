# Which joint kinds are there, and what does each expose?

Type: grilling
Status: open
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
