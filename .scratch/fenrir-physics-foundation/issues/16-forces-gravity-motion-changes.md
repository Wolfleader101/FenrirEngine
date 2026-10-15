# 16: Forces, gravity and motion changes

**What to build:** Game code can push bodies and shape the world's rules (spec: "FenrirPhysics: install, worlds, settings", "stepping"). `PhysicsWorld` gains `AddForce`, `AddImpulse`, `AddTorque` and `AddAngularImpulse` by entity, queued and applied before the next step; gravity per world at any time; capacities from `PhysicsConfig`, fixed once a world exists.

**Blocked by:** 12 (Physics tracer bullet).

**Status:** ready-for-agent

- [ ] Tests: an impulse gives the expected velocity change for a known mass; a force over several ticks accelerates as expected; torque and angular impulse spin a body.
- [ ] Forces on an entity without a body, or in a scene without a world, do nothing (no crash).
- [ ] Setting gravity on one scene's world does not affect another's; zero gravity leaves bodies floating.
- [ ] Exceeding a capacity is reported (logged) rather than crashing.
