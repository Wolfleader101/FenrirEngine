# 09: Breaking and JointBroken

**What to build:** Joints snap under load and game code hears about it (spec: "FenrirPhysics: joint state and breaking", "stepping order within PostTick" step 5b and 6, "debug lines").

- `breakForce` (N) and `breakTorque` (N·m), infinite by default, compared against the magnitudes of `GetJointState`'s `force` and `torque`, every part included. One step over breaks it, with no averaging. Only awake, enabled joints with a finite threshold are checked.
- Step 5b: in joint entity order, physics disables overloaded joints in the backend, writes `enabled = false` into the component and its private copy (so it is not mistaken for a game edit), and queues `JointBroken { joint, bodyA, bodyB, force, torque }` (`bodyB` empty for the world). Events are sorted by joint entity and sent once per step with contacts and triggers. Sent only for threshold breaks, never for game disables. No opt-in tag.
- A broken joint is a disabled one: it stays on its entity, its bodies collide again from the next step, and repair is `enabled = true` with no cooldown; a joint still overloaded breaks again with another event.
- Disabled (broken) joints' axes draw dimmed in `Joints` debug lines.
- The Joints (C++) scene adds B toggling a 15 N `breakForce` on chain joint 1 and R repairing disabled joints.

**Blocked by:** 05 (Joints debug lines); 08 (Joint state).

**Status:** resolved (built and tested headless on branch `claude/joints-breaking-7b513f`; the DemoApp hand check is given to the user)

- [x] Tests (through `EventReader<JointBroken>`): one step over a threshold breaks the joint with one event naming the joint, its bodies and the load; several breaks in one step are sorted by joint; an infinite threshold never breaks.
- [x] Tests: a game disable sends nothing; repair while overloaded breaks again with another event; a broken joint's bodies collide again; lowering a threshold wakes the pair.
- [x] Tests: a disabled joint's axes are dimmed in the debug lines.
- [ ] B then a swing breaks chain joint 1 with one event and it draws dimmed; R repairs it (checked by hand).

Found while building: chain joint 1 holds the seven links below it, about 69 N, so a 15 N `breakForce` breaks it on the step after B, with no swing needed. R then repairs it into a joint still over 15 N, which breaks again at once with another event; B again (no threshold) and R repairs it for good. A joint woken from sleep reads a low load for its first step (no warm starting), so a threshold lowered on a sleeping joint can take a step or two longer to break; the tests check that no step over the threshold passes unbroken rather than counting steps.
