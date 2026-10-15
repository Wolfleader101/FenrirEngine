# 12: Physics tracer bullet: a box falls onto the ground

**What to build:** FenrirPhysics exists end to end, thinly: a dynamic box falls onto a static ground and rests, a kinematic paddle moved in `Tick` pushes it, and editing a box's `Transform` teleports it, in tests and in a DemoApp physics scene (spec: "FenrirPhysics: the seam", "components", "install, worlds, settings", "stepping", "build"; ADR-0010, ADR-0011). The spike on `prototype/fenrir-physics-spike` built this slice crudely; build it properly.

- Jolt v5.6.0 as a static submodule linked `PRIVATE`, with the spec's option overrides and warning isolation; root option `FENRIR_ENABLE_PHYSICS` (default ON).
- The private concrete backend class, implemented only in the Jolt directory.
- `Physics::Install(App&, PhysicsConfig)` returning the owner of the backend's process-wide state; destroying it removes every scene's world first. The owner must outlive `App::Run`.
- One physics world per scene as a scene resource, created the first time the active scene has a physics component (scanning existing colliders), paused while inactive, reached by `PhysicsWorld::Of(scene)`.
- `RigidBody` (motion type, mass, damping, gravity scale, velocities) and `Collider` holding a `CollisionShape` tagged union with box and sphere (checked `Is<T>`/`As<T>`, implicit conversions).
- The `PostTick` pipeline: drain component watches (net effect per entity), diff in stable entity order (teleport static/dynamic, `MoveKinematic` every step for kinematic bodies, velocities, shape and scale), step, write back through `SetWorldTransform` and re-read the copy through `GetWorldTransform`. Single-threaded stepping.
- The DemoApp physics scene: ground, coloured falling boxes, kinematic paddle.

**Blocked by:** 02 (Fixed-step phases and a cap on ticks per frame); 03 (Stable scenes, and DestroyScene keeps the active scene); 04 (Scene resources); 05 (World transforms everywhere); 06 (Component watches).

**Status:** ready-for-agent

- [ ] A FenrirPhysics test target (Catch2, through the frame seam) pins: a box and a sphere fall and rest on a static ground (with a tolerance for penetration slop); a collider with no `RigidBody` is static; a kinematic body moved in `Tick` pushes a dynamic one and stops when not moved; writing a dynamic body's `Transform` teleports it; writing its velocity changes it; switching motion type and shape kind take effect; two runs give bit-identical results.
- [ ] A scene without physics components gets no world; a world is created when one appears; an inactive scene's world does not step and resumes when active again; destroying the scene or the `Physics` owner ends it cleanly.
- [ ] No public FenrirPhysics header names or includes Jolt, and Jolt's include path reaches no target outside FenrirPhysics.
- [ ] FenrirPhysics and Jolt build in the Clang and MSVC x64 debug and release presets with no warnings from Jolt's sources; `FENRIR_ENABLE_PHYSICS=OFF` builds the engine without them.
- [ ] The DemoApp physics scene shows boxes falling, stacking and being pushed by the paddle (checked by hand).
