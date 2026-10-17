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
