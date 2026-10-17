# What does C# get for joints?

Type: grilling
Status: open
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
