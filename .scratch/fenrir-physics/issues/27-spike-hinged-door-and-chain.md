# Spike: a hinged door and a hanging chain through the proposed model

Type: prototype
Status: resolved
Blocked by: 22, 23

## Question

Does the joint model from [How are joints modelled in the ECS?](22-joints-in-the-ecs.md) and the kinds from [Which joint kinds are there, and what does each expose?](23-joint-kinds-and-settings.md) work in practice? Build a throwaway spike in the DemoApp on a `prototype/fenrir-physics-joints-spike` branch: a door on a hinge (with a limit and a motor) hung from a static frame, and a chain of boxes hanging from a point fixed to the world, made through the proposed public interface with Jolt behind the seam. Check it by hand in the DemoApp, as "Spike: a box dropping onto a plane through the proposed seam" was.

React to it: does the interface read well; are anchor frames intuitive under parented and scaled bodies; what does editing a setting cost (rebuilds); does anything about Jolt leak; do jointed bodies stay stable. Record the lessons the spec needs.

The spike's scene may later become one of the joints demo scenes (see "What do the joints demo scenes show, and how do several scenes switch?").

From "What happens at a joint's edges?" (26): try the risky edges by hand. Destroy a chain link's entity mid-swing (the joints touching it go inactive with one warning each; the rest of the chain wakes and falls; no crash in Release, since constraints are removed before bodies). Teleport the door alone (it is pulled back to its frame) and then the door and frame together (no jolt). Scale and reshape a link (the anchors stay put). Check that a live motor edit wakes a sleeping door, and that solve order set from joint entity order through Jolt's private constraint priority actually changes nothing visible after removals. Record how violent a single-body teleport is.

## Answer

**Verdict: the joint model works through the seam, and nothing about Jolt reaches the public interface.** The user ran the DemoApp's "Joints (spike)" scene (F4) and judged that it works. It has a door on a limited, motorised hinge in a static frame, and an eight-link ball-jointed chain hanging from a world point. The scene logs what each key does.

Prototype (primary source, never merged): branch `prototype/fenrir-physics-joints-spike` (`ee05236`). The slice covers:

- `Joint` on joint entities: the door holds its own hinge, and each chain joint has its own entity.
- The `JointKind` union with `BallJoint` and `HingeJoint`: `JointLimits`, `JointMotor`, `JointSpring` and friction.
- `FrameFromWorld`, `enabled`, `collideConnected`, `breakForce` and `breakTorque` with `JointBroken`, and `PhysicsWorld::JointState`.
- Ticket 26's edges: inactive joints, constraints removed before bodies, shape changes notified, scale recreating the constraint, warm start reset on teleport, waking on every `Joint` change, and solve order from joint entity order.

The headless check `FenrirPhysicsTests "[joints-spike]"` has seven cases. Keys 0–9, B, R, P and L drive the hand checks. `FENRIR_JOINTS_SPIKE=keys` starts the app in the scene and presses every key in turn.

**Evidence:**

- **Seam:** Jolt is still included only in `JoltBackend.cpp`. The public `Joints.hpp` and `PhysicsWorld.hpp` name nothing from Jolt. The backend gained `CreateJoint`, `DestroyJoint`, `UpdateJoint`, `ResetWarmStart` and `Load`.
- **Builds:** clang-x64-debug and clang-x64-release. All 136 FenrirPhysics test cases pass in both, the foundation's included. MSVC and Linux were not built.
- **Behaviour in the headless check and in Debug and Release smoke runs:**
  - The chain hangs from its hook, and the top joint reads the chain's weight (49.0 N for five 1 kg links).
  - Destroying a link mid-swing gives exactly two warnings, one for each joint naming it, and no error. In Release, nothing crashes. The links below fall, and the inactive joints read the zeroed state.
  - A hinge stays at its limit, and its Position motor reaches its target to within 0.005 rad.
  - Editing the motor of a sleeping door wakes it.
  - A joint over its `breakForce` sends one `JointBroken` and is disabled. Repairing it while it is still overloaded breaks it again on the next step, with another event.
  - Scaling a link, adding a compound lump to a link and reshaping a link keep the anchors on their bodies: the drawn frames coincide again once the chain settles.

**Lessons for the joints spec:**

1. **`JointState` names both a type and a member.** Inside `PhysicsWorld`, the member `JointState(joint)` hides the struct `JointState`, so the declaration and definition must write `Fenrir::JointState`. Callers are unaffected, but the spec should pick one name to avoid it: for example, the query `GetJointState` or the struct `JointReading`. Ticket 28's C# method meets the same clash.
2. **A motor at the default `maxForce = infinity` beats a hard limit.** A Velocity motor at 2 rad/s holds a hinge about 0.2 rad past a 1.0 rad limit, because Jolt solves the motor before the limit. With `maxForce = 50` N·m, the hinge sits on the limit. The spec must choose one of three fixes:
   - a finite default `maxForce`;
   - the limit always winning, done privately: physics clamps the velocity target, or drops the motor at the limit;
   - documenting that an infinite motor overrides limits.

   Ticket 23's "a position target outside an enabled limit is clamped" covers Position mode only.
3. **Computing torque is backend-specific and approximate.** Jolt reports a hinge's rotation impulse along two axes it works out privately each step. To return torque, the backend rebuilds those axes the way `HingeRotationConstraintPart` does, from the poses after the step. This stays inside `src/Backend/Jolt/`, but each new kind (slider, swing-twist) needs its own conversion. The spec should say that `force` and `torque` sum the solver's impulses and are approximate within a step.
4. **Teleports are corrected by position, not by impulse.** A door teleported 0.7 m alone returns to its hinge within 10 steps (1/6 s). It gains no velocity: its peak speed is about 1e-5 m/s, and the joint force stays at its 9.8 N weight. So a single-body teleport against a static anchor is not violent. Because position correction is not an impulse, `JointState` and breaking do not see a teleport: a teleport never breaks a joint. Door and frame teleported together line up after 1 step, with no jolt. The spec should state both facts.
5. **Solve order works as decided.** Removing a joint made before the chain leaves the chain bit-for-bit identical, compared with only disabling that joint. With the priority off (`FENRIR_JOINTS_SPIKE_NO_PRIORITY`), the two runs differ. Keep the private priority from joint entity order.
6. **Chains stretch a little.** While an eight-link chain swings, ball-joint frames drift 1–2 cm apart, and less than 1 mm once it settles. That is acceptable, so no world-level solver setting is needed yet (ticket 23 left that open). With `collideConnected = true`, overlapping links push their frames up to 14 cm apart, as expected.
7. **Hinge angle sign follows which body is A.** `position` is `frameB` relative to `frameA`, so the door is hinged with the static post as `bodyA` to read a positive opening angle. A body hinged to the world must have the world as `bodyB`, so its angle reads with the opposite sign. The spec should document this, or measure from the moving end.
8. **Anchors under parented and scaled bodies work, but frames are not readable by eye.** `FrameFromWorld` is the way to author them. Under the post's (0.2, 2.2, 0.2) scale, the door's `frameA` position is (0.5, −0.023, 0). Hand-written frames on scaled bodies are a trap. The demo should draw the frames: the spike draws each joint's two frames as axes, which proved the most useful hand-check aid. This argues for ticket 29's `Joints` debug-line category.
9. **Most edits are live, and rebuilds are rare.** Only the kind, a body, a frame or a body's scale recreate the constraint. Motor, limit, friction, `enabled`, `collideConnected` and threshold edits are applied live, and each wakes both bodies through one `UpdateJoint`. Rebuilds were not felt in the scene.
10. **Details for the spec:**
    - **Another scene:** `body != entities.HandleOf(body.GetId())` detects a body in another scene, because `Entity`'s `==` compares the list. No ECS change is needed.
    - **Empty is not destroyed:** an empty `bodyB` is `Entity()`, which is distinct from a destroyed handle. `FrameFromWorld` must treat only an empty `bodyB` as the world: the spike treated any invalid `bodyB` as the world, which is wrong.
    - **Events:** `JointBroken` must be read each frame, like every event: the spike's first headless check missed it after 30 frames.
    - **Not tried:** removing and re-adding a `Joint` in one tick. The spike's net-effect diff keeps the constraint, so the spec should say whether that rebuilds it.
