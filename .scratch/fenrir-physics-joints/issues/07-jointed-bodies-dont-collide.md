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

**Status:** resolved

- [x] Tests: jointed bodies don't collide by default; `collideConnected` lets them; bodies two apart in a chain collide; the layer rule wins over `collideConnected`; a trigger child of a jointed body is not excluded.
- [x] Tests: disabling the joint ends the exclusion on the next step; toggling `collideConnected` between touching bodies sends `ContactEnd`, then `ContactBegin`; queries are unaffected.
- [x] A chain with excluded pairs is bit-identical single- and multi-threaded.
- [ ] Links two apart collide; 9 pushes overlapping links apart; the door's trigger child reports its post (checked by hand).

## Answer

Built as specified. Public: `Joint::collideConnected` is now applied, documented on `Joint`. Private: `Backend::JointDesc::collideConnected` and `UpdateJoint`'s new `collideConnected` argument; in `JoltBackend.cpp`, `JoinedPairs`, one Jolt `GroupFilter` over a pair → count table, and `World::Impl::SetExcludes`. Tests are in `packages/FenrirPhysics/Tests/JointCollisions.cpp`.

- **Where the rule lives:** the backend decides whether a joint excludes its pair (enabled, `collideConnected` false, `bodyB` not the world) whenever a constraint is made, updated or destroyed, so `PhysicsWorld::Impl` only passes the field through. Tickets 09 and 10 get the same rule for free when they disable or drop a constraint.
- **Groups:** a body gets a collision group naming the filter, its group ID its body index, when it enters its first excluded pair, and loses it when it leaves its last. The filter allows any pair where either body has no group of this filter, so triggers (their own bodies, never jointed) are never excluded.
- **Threads:** the table and the groups change only in backend calls made on the stepping thread between steps (`SyncJoints` in `PostTick`, and constraint removal before body destruction); Jolt's job threads only read them during the step.
- **Contact events:** nothing special. A joint change wakes both bodies, so Jolt's next step no longer finds (or finds again) the pair, and the usual `ContactEnd` and `ContactBegin` follow.
- **Demo:** chain links are back to full length (0.6 on 0.5 spacing), overlapping their neighbours; 9 toggles `collideConnected` on the joints between links (also a `FENRIR_DEMO_KEYS` smoke key); a trigger child at the door's hinge edge logs `TriggerEnter` with the post.
- **Checked by running:** clang debug and release, every suite. MSVC x64 debug and release (scripting off), `FenrirPhysicsTests`. No build adds a warning in the code this ticket touched. `FENRIR_DEMO_SCENE="Joints (C++)" FENRIR_DEMO_KEYS=1` logs the door trigger entered by `Post (hinge)` and key 9 letting the links collide, with no errors or warnings. With scripting on, the C# Playground's own `TriggerEnter` logger reports the door trigger too.
- **Left for the hand check:** links two apart collide as the chain folds; 9 pushes overlapping links apart and back; the door's trigger reports its post.
