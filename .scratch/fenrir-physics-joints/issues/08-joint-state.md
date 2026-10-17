# 08: Joint state

**What to build:** Game code reads how a joint sits and how hard it pulls (spec: "FenrirPhysics: joint state and breaking", "the Jolt backend"). `PhysicsWorld::GetJointState(Entity joint) -> JointState { force, torque, relative, position }` reads the last completed step, like `IsSleeping` and `Bounds`; never written into the component; safe from parallel systems.

- Pose readings are computed by physics from the last step's simulated poses and the frames, for every kind, never from interpolated `Transform`s or the backend's per-kind readings. `position` is the hinge angle (wrapped to (-π, π], no turn count), the slider offset along X, the distance length, and 0 otherwise. Swing-twist's `relative.rotation` is in the same terms as its motor's `targetRotation`.
- `force` and `torque` sum every part of the joint (position, rotation, limits, motor), applied to `bodyB`, converted privately from the backend's per-part impulses over the step time. Each kind needs its own conversion; the spike built hinge and ball only, so test each kind's steady load early. Documented as approximate within a step.
- Zeroed for a non-joint and a joint not yet stepped. While asleep, force and torque keep their last awake value; while disabled they are 0. Pose readings are always current.
- Documented: the hinge angle's sign follows which body is A; a body hinged to the world reads with the opposite sign.
- The DemoApp Joints (C++) scene writes each joint's readings into its entity name every frame (for example "Door hinge: 0.73 rad, 12 N"); key 0 logs every joint's full state.

**Blocked by:** 04 (Distance, slider and swing-twist kinds).

**Status:** ready-for-human (built and tested headless on branch `claude/joints-state-14d8ff`; the DemoApp hand check is left)

- [x] Tests: the chain's top joint reads its weight (49.0 N for five 1 kg links); a hinge reads its angle with the documented sign; a slider its offset; a distance joint its length; each kind's steady load is right.
- [x] Tests: zeroed for a non-joint and an unstepped joint; force and torque kept while asleep and 0 while disabled; pose readings current in both cases.
- [ ] The door's and chain's names show their readings live and 0 logs every joint (checked by hand).
