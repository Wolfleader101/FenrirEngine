# 03: Joints tracer bullet: a hinged door and a hanging chain

**What to build:** FenrirPhysics has joints end to end, thinly: a door hinged to a static post and an eight-link ball chain hung from a world point, in tests and in a DemoApp Joints (C++) scene on F4 (spec: "the joint component and kinds", "frames", "stepping order within PostTick", "the Jolt backend"; ADR-0013, ADR-0010, ADR-0011). The spike on `prototype/fenrir-physics-joints-spike` built this slice crudely; build it properly against the spec's API. The spike branch is reference only.

- `Joint` on a joint entity (`bodyA`, `bodyB`, `frameA`, `frameB`, `kind`, `enabled`, plus `collideConnected`, `breakForce` and `breakTorque` as fields with their defaults, wired up in later tickets). Any entity may be a joint entity, including one of its bodies; physics never reads the joint entity's `Transform`. An empty `bodyB` is the world, and the world is only ever `bodyB`.
- `JointKind`, a plain-data tagged union in `CollisionShape`'s pattern (implicit conversions, `Type()` returning `JointType`, asserting `Is<T>()`/`As<T>()`), defaulting to `FixedJoint {}`, with `FixedJoint`, `BallJoint` and `HingeJoint`. Hinge limits, motor and friction come in ticket 06.
- Frames local to each body's origin; positions follow the body's world scale, rotations do not. Physics converts them privately to centre-of-mass frames and builds backend constraints in local space. Each frame's local X is the hinge axis; hinge angles are signed by the right-hand rule.
- `FrameFromWorld(bodyA, bodyB, anchor) -> JointFrames`, a free function through `GetWorldMatrix`, needing only the scene. Only an empty `bodyB` is the world; a body that is not alive gives a debug warning and identity frames.
- `PostTick`: the `Joint` watch drained with the body watches (constraints removed before their bodies are destroyed or rebuilt, created after), joints diffed after bodies in stable entity order, live edits pushed and kind, body, frame or scale changes recreating the constraint. A `Joint` removed and re-added in one drain is rebuilt; added then removed does nothing. Every diffed change wakes both bodies. Destroying a world removes every constraint before any body.
- Solve order follows joint entity order through a private backend priority, never exposed.
- The DemoApp Joints (C++) scene registers on F4 (Default ⇄ Joints (C++)): the door (a hinge on a static post scaled (0.2, 2.2, 0.2) under a "Door frame" entity, the post as `bodyA`) and the chain. No keys yet beyond what the scene needs to be looked at.

**Blocked by:** 02 (DemoApp scene switcher).

**Status:** resolved

- [x] `FenrirPhysicsTests` (through the frame seam): fixed, ball and hinge joints hold their bodies at the rest pose; a hinge turns about its X axis and a ball turns freely; a joint to the world works; a body in several joints, and a joint entity that is one of its bodies, both work.
- [x] Frames on scaled and rotated parents stay on the body; `FrameFromWorld` round trips, treats only an empty `bodyB` as the world, and warns with identity frames for a dead body.
- [x] A live edit reaches physics on the next tick; kind, body, frame and scale changes recreate without moving the rest pose; a `Joint` removed and re-added in one tick is rebuilt; every edit wakes a sleeping pair.
- [x] Removing an earlier joint leaves a chain bit-identical to one where it was never created; two runs are bit-identical, single- and multi-threaded.
- [x] Reading `JointKind` as the wrong kind asserts; `Joint{}` has the spec's defaults.
- [x] No public FenrirPhysics header names Jolt; Clang and MSVC x64 debug and release presets build without warnings.
- [ ] The Joints (C++) scene on F4 shows the door swinging on its post and the chain hanging from its world point (checked by hand).

## Answer

Built as specified. Public: `FenrirPhysics/Joints.hpp` (`JointFrame`, the three kinds, `JointType`, `JointKind`, `Joint`, `JointFrames`, `FrameFromWorld`). Private: `src/Joints.cpp` (the joint drain, the body-to-joints index and the joint diff, as `PhysicsWorld::Impl` members), the backend's `CreateJoint`, `DestroyJoint` and `UpdateJoint`, and `src/Same.hpp` (the diff's bit-for-bit comparisons, now shared by bodies and joints). The scene is `examples/DemoApp/src/JointsScene.hpp`.

- **Order within `PostTick`:** `SyncBodies` drains the `Joint` watch with the body watches (a removed or re-added joint loses its constraint there) and removes the constraints of every joint touching a body it is about to destroy or rebuild, through the index; then `PushEdits`; then `SyncJoints` creates, recreates or updates each joint's constraint in joint entity order. Joints come after the body edits because a body's scale decides recreation.
- **A joint naming no made body entity** has no constraint, silently, until it does. Ticket 10 adds the warning, the wake and the full inactive rules.
- **`FrameFromWorld`'s debug warning** goes through the app physics was last installed on (a weak pointer in `Physics.cpp`), since the function is given no app.
- **The wrong-kind assert** is checked by `JointKindWrongView`, a program of its own that CTest runs in debug builds and passes on the assert's message. It turns off the Windows debug runtime's abort dialog: without that, an assert in a test binary hangs until CTest's timeout.
- **Solve order:** the "removing an earlier joint" test fails with the priority set to 0, and passes with each constraint's priority set to its joint entity.
- **Demo chain:** links are 0.4 long on 0.5 spacing so neighbours do not touch while bodies a joint names still collide (ticket 07).
- **Checked by running:** clang debug and release, every suite. MSVC x64 debug and release (scripting off), `FenrirPhysicsTests` and the assert test. No build adds a warning in the code this ticket touched; the FenrirPhysics warnings that remain were there before. `FENRIR_DEMO_SCENE="Joints (C++)"` starts in the scene and logs its key help, with no errors or warnings.
- **Found and fixed:** under MSVC x64 release, every test that casts a ray crashed with an access violation, on the commit before this ticket too. `Backend::FunctionRef`'s constructor template matched a non-const `FunctionRef` better than the copy constructor. So `RayCollector`'s copy of its sink referred to the constructor's by-value parameter, which was gone by the time the cast called it. The template now excludes `FunctionRef`, and the whole `FenrirPhysicsTests` suite passes under MSVC x64 release. The existing raycast tests are the regression tests: no FenrirPhysics test may include the private backend header, and the dangling call only fails visibly where the optimiser reuses the stack slot.
- **Left for the hand check:** the door swinging and the chain hanging, by eye.
