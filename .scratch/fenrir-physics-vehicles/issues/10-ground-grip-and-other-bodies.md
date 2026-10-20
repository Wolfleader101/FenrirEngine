# 10: Ground, grip and other bodies

**What to build:** Ice, grass and tarmac grip differently with no surface table, layers alone decide what is ground, and vehicles meet dynamic bodies, moving platforms and characters by the spec's rules (spec: "FenrirPhysics: ground, grip and other bodies", "the Jolt backend" (the collision tester's filters, the body filter)).

- **Ground filtering:** each wheel's probe filters exactly as the chassis's `CollisionFilter` does (two-sided layer-and-mask rule), with no separate probe mask. Probes always skip triggers, the vehicle's own chassis and every character's inner body (added to the body filter, still lock-free on job threads). They ignore joints, so `collideConnected` never hides ground.
- **Characters are never ground:** a car meeting a pedestrian meets them with its chassis. A character pushing a vehicle pushes with its `pushForce`; a vehicle hitting a character stops at an immovable kinematic wall; a character standing on a chassis rides it and presses with its weight. No new behaviour, only tests and docs.
- **Per-surface grip:** the tyre's curve value (or a track's friction) combines with the ground body's `PhysicsMaterial::friction` as the geometric mean, through Jolt's default combine (spike lesson 7). Documented: ice must be set very low; the chassis's own material is only what bodies touching the chassis feel.
- **Other bodies:** a wheel on a dynamic body pushes, loads and wakes it; on kinematic ground slip is measured against the ground's velocity, so a vehicle rides a platform through grip; queries see only the chassis and name its entity, and `QueryFilter::ignoreBody` takes it; a body falling onto a wheel's mesh passes through to the chassis. Stated limit documented: over light bodies the chassis dips.
- **DemoApp (C++):** the spike's ice (0.05) and grass (0.3) lanes, the wall of 20 kg crates, a pedestrian (a standing character on the start straight, which the autopilot avoids), and a ferry (a kinematic platform at 2 m/s shuttling across a gap; the autopilot's lap crosses it). Wheel readouts show "sliding" past the tyre's `slideSlip`.

**Blocked by:** 03 (Wheel settings, probe and tilt limit).

**Status:** resolved

- [x] `FenrirPhysicsTests`: probes filter by the chassis's `CollisionFilter`; they skip triggers, the chassis and a character's inner body.
- [x] A car driven at a character is stopped by the chassis with no wheel on the capsule.
- [x] Grip: the spike's table within tolerance: on ice the wheels spin with slip past `slideSlip`; grass is slower than tarmac.
- [x] A wheel on a dynamic box pushes it; a parked vehicle rides a kinematic platform; queries name the chassis and `ignoreBody` skips it.
- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] On ice the wheels spin ("sliding"); grass is looser than tarmac; the car shoves the crates with the stated dip; the pedestrian stops the car at the chassis and the wheels never climb them; a parked vehicle rides the ferry (checked by hand).

**Resolved by:** PR (branch claude/zealous-euler-8ec9ac), stacked on #57 (ticket 13, on #56). The proof is `FenrirPhysicsTests`' `[ground]` cases (`Tests/VehicleGround.cpp`) and the `FENRIR_DEMO_SCENE="Vehicles (C++)" FENRIR_DEMO_KEYS=1` smoke run, whose autopilot now drives a whole lap: the ice and grass lanes, the crates and the ferry both ways. The last box (feeling the grip, the dip and the ferry by hand) is left for a person. Findings and choices:
- The backend's probe body filter drops the chassis and any body in the character listener's map of inner bodies, which is written only between steps, so job threads read it without a lock. Triggers were already skipped by Jolt's testers (they ignore sensors), and the layer rule is the chassis's own object layer; ticket 03's `Retest` already remade the tester on a relayer.
- Without the filter a wheel dropped over a standing character stood on its capsule; the `[ground]` case for it fails with the filter removed.
- Grip needed no code: Jolt's default combine is already the geometric mean. The spike's table is matched closely: 34.6, 71.6 and 80.4 km/h after 5 s at full throttle on 0.05, 0.3 and 0.5, and 21.5, 35.9 and 38.5 m braking.
- A car stops at a standing character only while game code sets the character's velocity each tick, as games do. A character left with no game code keeps the velocity a push wrote back, and drifts away from the car. The demo's pedestrian is held still by game code each tick for this reason.
- The demo's autopilot waypoints gain a reach distance and an optional ferry dock. Before a waypoint with a dock, the car waits braked until the ferry is at that dock (to drive aboard, until it has only just docked). Riding across braked shows a parked car carried through its tyres' grip.
