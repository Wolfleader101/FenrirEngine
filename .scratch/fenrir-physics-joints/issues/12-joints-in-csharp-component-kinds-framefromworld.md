# 12: Joints in C#: component, kinds and FrameFromWorld

**What to build:** C# systems build joints exactly as C++ does (spec: "FenrirPhysicsScripting and Fenrir.Physics"; ADR-0009).

- `Fenrir.Physics` gains `Joints.cs`: `[NativeComponent("Fenrir.Physics.Joint")] public struct Joint : IComponent` with `EntityLink BodyA, BodyB` (both `EntityLink.None` from `new`), `JointFrame FrameA, FrameB`, `JointKind Kind = new()`, `bool Enabled = true`, `bool CollideConnected = false`, `float BreakForce, BreakTorque = float.PositiveInfinity`; and every kind and setting type with the C++ names in PascalCase and the C++ defaults.
- `JointKind` is hand-written in `CollisionShape`'s pattern: explicit layout with a private one-byte tag, `Type`, one checked `[UnscopedRef] ref` view per kind named after it (`Fixed`, `Ball`, `Distance`, `Hinge`, `Slider`, `SwingTwist`), implicit conversions from each kind, `InvalidOperationException` on the wrong view. `new JointKind()` is C++'s default kind. It is the surface a `[TaggedUnion]` generator would emit.
- `Joint.FrameFromWorld(bodyA, bodyB, anchor)` and `Joint.FrameFromWorld(bodyA, anchor)` for the world, both a native function returning `JointFrames`, so the world matrix and empty-is-world rule live once in C++.
- `FenrirPhysicsScripting` maps `Joint` through stand-ins (an `EntityLinkView` where each `Entity` sits, a plain mirror of `JointKind`'s union) and registers the native function. FenrirPhysics still names nothing from Iris.
- `default(Joint)` names entity 0 with no scene, so it reads inactive with a warning, without crashing.

**Blocked by:** 01 (Writable entity links in C#); 06 (Limits, motors, springs and friction).

**Status:** ready-for-agent

- [ ] `PhysicsScriptingTests` with a `Joints.cs` fixture: `Joint` validates against its native layout; `new Joint()` equals `Joint{}` (empty links, infinities, kind defaults).
- [ ] A C# system creates two bodies and a hinge joint between them in one system, and the hinge holds after playback.
- [ ] Assigning `Kind = new HingeJoint { ... }` and editing `Kind.Hinge.Motor.TargetVelocity` reach physics; reading `Kind.Slider` on a hinge throws.
- [ ] `FrameFromWorld` matches C++ under a scaled, rotated parent; the world overload equals C++ with an empty `bodyB`.
- [ ] `default(Joint)` reads inactive with a warning and does not crash.
