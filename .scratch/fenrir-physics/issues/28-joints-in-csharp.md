# What does C# get for joints?

Type: grilling
Status: resolved
Blocked by: 22, 23, 24

## Question

What does the physics scripting extension (`FenrirPhysicsScripting`, contract `Fenrir.Physics`) give C# for joints? Builds on [How are joints modelled in the ECS?](22-joints-in-the-ecs.md), [Which joint kinds are there, and what does each expose?](23-joint-kinds-and-settings.md) and [What joint state can game code read, and how do joints break?](24-joint-state-and-breaking.md), and follows "What does the physics scripting extension give C#?" for the foundation.

- **Components:** joint components exposed and writable from C#, including fields naming entities (mapped as `Relationship`'s are).
- **Tagged union:** if a joint holds a kind union, it is the second one; "How do C# components hold a tagged union?" deferred generator support until then. Hand-write it again, or build the `[TaggedUnion]` generator now?
- **Native functions:** any joint queries or calls (state read back, if not fields).
- **Events:** the break event through `EventReader<T>`.
- **Robustness:** the same no-op-with-warning rules as the foundation's native functions.
- **What the C# demo scene needs** that tests alone would not prove.

From "How are joints modelled in the ECS?" (ADR-0013): `Joint` holds `Entity` fields, `JointFrame`s and a `JointKind` tagged union, which makes it the second tagged-union component. A C# form of the `FrameFromWorld` helper is needed.

From "What joint state can game code read, and how do joints break?" (24): state is a query, not a field, so C# needs one native function for `PhysicsWorld::JointState(joint)` returning the plain-data `JointState { force, torque, relative (a JointFrame), position }`, zeroed for a non-joint. `Joint` gains `breakForce` and `breakTorque` (default infinity; the C# defaults must match). `JointBroken { joint, bodyA, bodyB, force, torque }` is read through `EventReader<JointBroken>`; `bodyB` is empty for the world. Repair is writing `enabled = true`, so physics writes `enabled` back and C# must see that write.

From "How do jointed bodies stop colliding?" (25): `Joint` gains `bool collideConnected` (default `false`; the C# default must match, as for `breakForce`/`breakTorque`). No native function or event: toggling it is a field write, and contacts it ends or starts arrive as the usual `ContactEnd`/`ContactBegin`.

From "What happens at a joint's edges?" (26): no new C# surface. An inactive joint (a named body gone, the same body twice, a non-body, another scene) keeps its component as written, logs one warning per transition, and reads as the zeroed `JointState`, so C# sees no difference from a non-joint there. Every field write wakes both bodies, so C# never needs a wake call for joints.

From "Spike: a hinged door and a hanging chain through the proposed model" (27): in C++ the member `PhysicsWorld::JointState` hides the `JointState` struct, so its declaration must qualify the type; a C# `PhysicsWorld.JointState(...)` method returning a `JointState` struct meets the same clash, so settle one name (e.g. `GetJointState`) for both. `FrameFromWorld` takes only an empty `bodyB` as the world (a destroyed one is not), and C#'s form must do the same. `JointBroken` must be read every frame, as any event.

## Answer

**C# gets every joint type the C++ side has, writable, in the foundation's patterns. Two pieces are new:**
- **Writable entity links**, a FenrirScripting prerequisite.
- **A second hand-written tagged union**, `JointKind`. The `[TaggedUnion]` generator is wanted and flagged, but not built here.

**Facts found in the code** (on `dotnet-scripting`):
- `Joint` is the first *writable* component with `Entity` fields. A native `Entity` is a 32-bit value plus a pointer to its entity list. C# sees it as a read-only `EntityLink` (`value`, `reserved`, `owner`) through `Detail::EntityLinkView` stand-ins, as `Relationship` and the contact and trigger events do. C# can read `Target` but cannot make a link.
- A link written with no `owner` would read to physics as "in another scene" (ticket 26's `body != entities.HandleOf(id)` check). The joint would go inactive with a warning.
- A zeroed `EntityLink` names entity 0. The empty link is `value = 0xFFFFFFFF` with a null owner.
- C# has only the lossy `Entity.WorldTransform`. The exact `GetWorldMatrix` is not reachable from C#.
- `Fenrir.Scripting.Generators` already holds six source generators. The union's field offset still has to be written by hand, because C# cannot see the C++ union's alignment.

### Decisions

1. **Writable entity links (FenrirScripting core, a prerequisite of the joints spec).**
   - `EntityLink` gains an implicit conversion from `Entity`. One new core native function fills `owner` with the active scene's entity list, and `owner` is never readable from C#.
   - It also gains `EntityLink.None`, the empty link.
   - So `joint.BodyA = door; joint.BodyB = EntityLink.None;` writes the bodies, and `joint.BodyA.Target` reads one back.
   - C# can name only entities of the active scene. An entity made by `Entity.Create()` earlier in the same system works, because its value is reserved at once.
   - Every later native component with an `Entity` field gets this for free.
   - Rejected: physics re-linking by id, which leaves C++ readers holding an `Entity` with a null list and crashes them. Also rejected: a read-only `Joint` with native setters, which breaks ticket 11's rule that every physics component is writable.
2. **`JointKind` is hand-written in `CollisionShape`'s pattern.** It has:
   - a private one-byte tag and each kind at the union's offset;
   - `Type`;
   - one checked `[UnscopedRef] ref` view per kind, named after the kind (`Fixed`, `Ball`, `Distance`, `Hinge`, `Slider`, `SwingTwist`), even for the empty kinds;
   - implicit conversions from each kind;
   - `InvalidOperationException` on the wrong view.

   That is exactly the public surface a `[TaggedUnion]` generator would emit, so a generator can later replace both unions without touching a caller.
   - **The generator is wanted, not dropped.** The user considers it important for the future. The trigger is now "a script author wants a union, or a third engine union appears". The spec's FenrirScripting tickets update the README's "Tagged unions" section to say a generator is planned with that trigger. The map carries it as fog, and a follow-up session was proposed for a FenrirScripting ticket.
   - Easy to reverse, so no ADR.
3. **The state query is `GetJointState`, in both languages.** The struct stays `JointState`, which resolves lesson 1's clash in C++ and in C#. In C# it is `JointState PhysicsWorld.GetJointState(Entity joint)`, returning the struct by value, as C++ does. It returns no `bool` with an `out`, because ticket 26 made a non-joint, an inactive joint and an unstepped one all read zeroed, so a bool would carry nothing.
4. **`FrameFromWorld` is a native function**, so the exact world matrix and the rule that only an empty `bodyB` is the world live once, in C++. It has two overloads:
   - `Joint.FrameFromWorld(Entity bodyA, Entity bodyB, JointFrame anchor)`;
   - `Joint.FrameFromWorld(Entity bodyA, JointFrame anchor)` for the world, so the world is never spelled as a magic `Entity`.

   Both return `JointFrames` (`FrameA`, `FrameB`). It needs only the active scene, not a physics world. It never throws: a body that is not alive gives a debug warning and identity frames, **in both languages** (this settles the C++ behaviour too). Rejected: pure C# on `WorldTransform`, which is approximate under rotated, non-uniformly scaled parents, the spike door's own case.
5. **`JointBroken` mirrors its native event**, like `ContactBegin`: `EntityLink Joint, BodyA, BodyB` and `Vector3 Force, Torque`, read through `EventReader<JointBroken>`. `BodyB.IsNull` means the world, with no extra helper. Repair is writing `Enabled = true` through `Ref<Joint>`. C# sees physics' `enabled = false` directly, because it reads the component in place. Its doc comment says to read it every frame (lesson 10).
6. **Robustness:**
   - `GetJointState` with no physics world, or on an entity that is not a joint, returns zeroed, with the foundation's debug warning once per native site.
   - An inactive joint returns zeroed with no extra warning, since ticket 26 already warns once per transition.
   - A joint created this tick reads zeroed until its first step.

   These rules go in `PhysicsWorld`'s remarks, as for the other functions.
7. **The C# `Joint` component** is `[NativeComponent("Fenrir.Physics.Joint")] public struct Joint : IComponent` with:
   - `EntityLink BodyA` and `BodyB`, both `EntityLink.None` when made with `new`;
   - `JointFrame FrameA` and `FrameB`;
   - `JointKind Kind = new()`;
   - `bool Enabled = true` and `bool CollideConnected = false`;
   - `float BreakForce` and `BreakTorque`, both `= float.PositiveInfinity`.

   C# names are the C++ names in PascalCase. The kinds and settings (`FixedJoint`, `BallJoint`, `DistanceJoint`, `HingeJoint`, `SliderJoint`, `SwingTwistJoint`, `JointSpring`, `JointLimits`, `JointMotor`, `JointMotorMode`, `JointType`, `JointFrame`, `JointFrames`, `JointState`) carry the C++ defaults. That includes `JointMotor.MaxForce`, whichever of lesson 2's fixes the spec picks. `new JointKind()` is C++'s default kind, and a test checks these defaults equal C++'s.
   - `default(Joint)` is the usual all-zero struct: `BodyA` names entity 0 with no scene, so the joint reads inactive with a warning. It does not crash.
   - Bools inside kinds (`JointLimits.Enabled`) stay. The README's caution is only for by-value copies from C++, so the spec states that `JointKind` never appears in an event or in a C++-to-C# call argument.
8. **Module shape:**
   - **FenrirScripting core:** the `EntityLink` conversion, `EntityLink.None`, the owner-filling native function and their tests. This is an explicit prerequisite, as the foundation listed its FenrirApp fixes.
   - **`Fenrir.Physics` contract:**
     - a new `Joints.cs` with the component, the union, the kinds and settings, `JointFrame`, `JointFrames`, `JointState` and `Joint.FrameFromWorld`;
     - `JointBroken` in `Events.cs`;
     - `GetJointState` on `PhysicsWorld`;
     - two new `PhysicsNative` entries.
   - **`FenrirPhysicsScripting`:** the mappings through stand-ins (an `EntityLinkView` where each `Entity` sits, and a plain mirror of `JointKind`'s private union, as for `CollisionShape`), the event registration and the two native functions. FenrirPhysics itself still names nothing from Iris.
9. **Tests**, as new `PhysicsScriptingTests` cases with a `Joints.cs` fixture:
   - `Joint`, `JointState` and `JointBroken` validate against their native layouts;
   - `new Joint()` equals C++'s `Joint{}`, including the empty links and the infinities;
   - a C# system creates two bodies and a joint entity in one system and links them, and after playback physics joins them (a hinge holds);
   - assigning a new kind, and editing a motor target through the hinge view, reach physics on the next tick;
   - the wrong view throws;
   - `GetJointState` reads the hinge angle, and reads zeroed with no world or for a non-joint;
   - `FrameFromWorld` matches C++ under a scaled, rotated parent, and the world overload equals C++ with an empty `bodyB`;
   - `EventReader<JointBroken>` sees one event when a joint breaks, and writing `Enabled = true` repairs it.
10. **What the C#-built scene must exercise** (input to ticket 29, which decides the contents, keys and switching):
    - C++ places only a marker. C# builds everything in one system: bodies, joint entities, links (some to entities created in that same system), and frames from `FrameFromWorld`, including the world overload.
    - A key drives a motor through a kind's view.
    - A key swaps a kind by assignment.
    - An on-screen readout from `GetJointState` every frame shows an angle and a load.
    - A breakable joint read through `EventReader<JointBroken>`, logged, with a key that repairs it.
    - Not the C++ scene's edge-case keys (destroy a link, teleport), which C# reaches unchanged.

**Glossary:** added **Entity link** and **Joint state**.
