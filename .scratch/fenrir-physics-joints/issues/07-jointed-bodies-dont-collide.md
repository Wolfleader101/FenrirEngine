# 07: Jointed bodies don't collide

**What to build:** The two bodies a joint names do not collide with each other unless the joint opts in with `collideConnected` (spec: "FenrirPhysics: jointed bodies not colliding", "the Jolt backend"). A live edit.

- **Only removes:** a pair collides only when the layer rule allows it and no enabled, active joint between them excludes it; `collideConnected = true` never overrides the filter.
- **Direct pairs only:** in a chain A–B–C, A and C still collide. With several joints on one pair, any one enabled joint with `collideConnected = false` excludes it.
- **Compounds and triggers:** every solid collider of both bodies is excluded; triggers are not, even a trigger child of a jointed body.
- **Lifetime:** only while enabled. A disabled joint excludes nothing from the next step; a joint to the world excludes nothing. (Broken and inactive joints are covered by tickets 09 and 10 through the same rule.)
- **Queries** ignore joints. An exclusion starting between touching bodies ends their contact with `ContactEnd`; one ending sends `ContactBegin` when they touch again.
- Backend: one private group filter over a pair → count table of excluding enabled joints, patched on the main thread in `PostTick` with the joint diff, so it is read-only while job threads call it. Only bodies in at least one excluding joint get a collision group.
- The Joints (C++) scene adds a trigger child on the door overlapping its post, logging `TriggerEnter` with the post, and key 9 toggling `collideConnected` on the chain.

**Blocked by:** 03 (Joints tracer bullet).

**Status:** ready-for-agent

- [ ] Tests: jointed bodies don't collide by default; `collideConnected` lets them; bodies two apart in a chain collide; the layer rule wins over `collideConnected`; a trigger child of a jointed body is not excluded.
- [ ] Tests: disabling the joint ends the exclusion on the next step; toggling `collideConnected` between touching bodies sends `ContactEnd`, then `ContactBegin`; queries are unaffected.
- [ ] A chain with excluded pairs is bit-identical single- and multi-threaded.
- [ ] Links two apart collide; 9 pushes overlapping links apart; the door's trigger child reports its post (checked by hand).
