# 13: Joint state and JointBroken in C#

**What to build:** C# scripts read a joint's load and angle and handle breaks as C++ does (spec: "FenrirPhysicsScripting and Fenrir.Physics").

- `JointState PhysicsWorld.GetJointState(Entity joint)`, by value, no `bool`/`out`, as a `PhysicsNative` entry. With no world, or on a non-joint, it returns zeroed with the foundation's debug warning once per native site; an inactive joint returns zeroed with no extra warning; a joint created this tick reads zeroed until its first step. These go in `PhysicsWorld`'s remarks. C# never needs a wake call.
- `JointBroken` in `Events.cs`: `EntityLink Joint, BodyA, BodyB`, `Vector3 Force, Torque`; `BodyB.IsNull` means the world; its doc comment says to read it every frame. Registered in `FenrirPhysicsScripting`. `JointKind` never appears in an event or a C++-to-C# call argument.
- Repair from C# is writing `Enabled = true`.

**Blocked by:** 09 (Breaking and JointBroken); 12 (Joints in C#: component, kinds and FrameFromWorld).

**Status:** ready-for-agent

- [ ] `PhysicsScriptingTests`: `JointState` and `JointBroken` validate against their native layouts.
- [ ] `GetJointState` reads a hinge angle; it reads zeroed with no world and for a non-joint, without throwing.
- [ ] `EventReader<JointBroken>` sees one break; writing `Enabled = true` repairs the joint.
