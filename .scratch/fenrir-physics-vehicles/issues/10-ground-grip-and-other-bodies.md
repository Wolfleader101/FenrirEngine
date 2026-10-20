# 10: Ground, grip and other bodies

**What to build:** Ice, grass and tarmac grip differently with no surface table, layers alone decide what is ground, and vehicles meet dynamic bodies, moving platforms and characters by the spec's rules (spec: "FenrirPhysics: ground, grip and other bodies", "the Jolt backend" (the collision tester's filters, the body filter)).

- **Ground filtering:** each wheel's probe filters exactly as the chassis's `CollisionFilter` does (two-sided layer-and-mask rule), with no separate probe mask. Probes always skip triggers, the vehicle's own chassis and every character's inner body (added to the body filter, still lock-free on job threads). They ignore joints, so `collideConnected` never hides ground.
- **Characters are never ground:** a car meeting a pedestrian meets them with its chassis. A character pushing a vehicle pushes with its `pushForce`; a vehicle hitting a character stops at an immovable kinematic wall; a character standing on a chassis rides it and presses with its weight. No new behaviour, only tests and docs.
- **Per-surface grip:** the tyre's curve value (or a track's friction) combines with the ground body's `PhysicsMaterial::friction` as the geometric mean, through Jolt's default combine (spike lesson 7). Documented: ice must be set very low; the chassis's own material is only what bodies touching the chassis feel.
- **Other bodies:** a wheel on a dynamic body pushes, loads and wakes it; on kinematic ground slip is measured against the ground's velocity, so a vehicle rides a platform through grip; queries see only the chassis and name its entity, and `QueryFilter::ignoreBody` takes it; a body falling onto a wheel's mesh passes through to the chassis. Stated limit documented: over light bodies the chassis dips.
- **DemoApp (C++):** the spike's ice (0.05) and grass (0.3) lanes, the wall of 20 kg crates, a pedestrian (a standing character on the start straight, which the autopilot avoids), and a ferry (a kinematic platform at 2 m/s shuttling across a gap; the autopilot's lap crosses it). Wheel readouts show "sliding" past the tyre's `slideSlip`.

**Blocked by:** 03 (Wheel settings, probe and tilt limit).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: probes filter by the chassis's `CollisionFilter`; they skip triggers, the chassis and a character's inner body.
- [ ] A car driven at a character is stopped by the chassis with no wheel on the capsule.
- [ ] Grip: the spike's table within tolerance: on ice the wheels spin with slip past `slideSlip`; grass is slower than tarmac.
- [ ] A wheel on a dynamic box pushes it; a parked vehicle rides a kinematic platform; queries name the chassis and `ignoreBody` skips it.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] On ice the wheels spin ("sliding"); grass is looser than tarmac; the car shoves the crates with the stated dip; the pedestrian stops the car at the chassis and the wheels never climb them; a parked vehicle rides the ferry (checked by hand).
