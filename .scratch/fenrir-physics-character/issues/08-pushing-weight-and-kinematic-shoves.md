# 08: Pushing, weight and kinematic shoves

**What to build:** A character pushes light boxes and not heavy crates, tips a seesaw and sinks a raft with its weight, is never pushed by loose bodies, and is shoved by doors and paddles at their own speed (spec: "FenrirPhysics: other bodies and the inner body" (pushing, standing on bodies, dynamic bodies never push, kinematic bodies shove), "the Jolt backend" (pushing and weight)).

- **Pushing:** a move that hits a dynamic body gives it an impulse towards the character's speed at the contact point, aiming at 0.9 × the closing speed, clamped by `pushForce` and never downward; `pushForce = 0` pushes nothing. Static and kinematic bodies are never pushed. Applied in step 3b, acting in this tick's step. No push reading in `state`.
- **Weight:** each move that ends on ground presses the ground body with `mass × gravity × dt` at the ground point (the world's gravity, scaled by how the ground normal faces it); `mass = 0` turns it off. The world's gravity is passed to the backend for this only.
- **Dynamic bodies never push a character:** they meet the inner body as an immovable kinematic wall. Knockback is game code writing `velocity`.
- **Kinematic bodies shove** a character at exactly their own speed into it: the move treats them as surfaces moving at their velocity, and that speed shows in the written-back `velocity` until game code overwrites it. Stated limits documented: a fast body may overlap for a tick; crushing is not detected.
- **Stated limit:** a body resting on a character (a box on its head) is not carried; it slips or is pushed out. A jump still pushes it up.
- **DemoApp:** boxes of 1, 20 and 100 kg (names show mass and speed); a hinged seesaw (its angle shown); a kinematic paddle sweeping a lane (paused by 3); B throws a ball at the standing character; H drops a 1 kg box on the head. The autopilot walks the boxes, seesaw and paddle lane.

**Blocked by:** 03 (Character tracer bullet).

**Status:** ready-for-agent

- [ ] Tests: a 1 kg box is pushed at pace; a 20 kg crate with friction 0.2 accelerates at about 3 m/s²; a 100 kg box with friction 0.5 does not move; `pushForce = 0` pushes nothing; no push is ever downward.
- [ ] Tests: a seesaw tips under `mass`, and not with `mass = 0`; a ball thrown at a standing character bounces off without moving it.
- [ ] Tests: a kinematic paddle shoves the character at its own speed, visible in the written-back `velocity`.
- [ ] 1 and 20 kg boxes move and 100 kg does not; the seesaw tips; the paddle shoves; B's ball bounces off; H's box is left behind (checked by hand).
