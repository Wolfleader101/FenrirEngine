# What does C# get for characters?

Type: grilling
Status: open
Blocked by: 32, 34, 35, 36

## Question

What does the physics scripting extension (`FenrirPhysicsScripting`, contract `Fenrir.Physics`) give C# for characters? Builds on [How is a character body modelled in the ECS?](32-character-bodies-in-the-ecs.md), [What does game code read back from a character, and which events?](34-character-state-and-events.md), [How does a character interact with other bodies?](35-characters-and-other-bodies.md) and [What are the motion-test and sweep queries, and how does a character change shape?](36-motion-tests-and-shape-changes.md), and follows "What does C# get for joints?".

- **Components:** the character component writable from C#.
- **Moving:** if a move is a native call rather than a field, how a C# system calls it within ADR-0009's rules.
- **State and events:** ground state and any character events through `EventReader<T>`.
- **Queries:** the motion-test queries as native functions with caller-supplied spans.
- **Robustness:** the foundation's no-op-with-warning rules.
- **What the C# demo scene needs** that tests alone would not prove.

From "How is a character body modelled in the ECS?" (ADR-0014): a character is driven by writing `CharacterBody::velocity`, a field, so C# needs no native move function (ADR-0009). `CharacterBody` holds plain fields (`velocity`, `enabled`, live settings) and no tagged union, so it maps directly. Physics never adds or removes components on a character, so nothing a C# system reads appears or disappears on landing. Inactive and disabled characters follow the foundation's silent no-op rules for native queries about them.

From "How does a character move each tick?": `CharacterBody` gains plain live fields `maxSlope`, `stepHeight`, `floorSnap`, `stopOnSlope` and `interpolate`, so it still maps directly. Gravity is game code's: a C# system adds `PhysicsWorld` gravity times `dt` to `velocity`, so the world's gravity must be readable from C# (it already is, per "What does the physics scripting extension give C#?"). Carried velocity and ground velocity are ticket 34's state.

From "What does game code read back from a character, and which events?": `CharacterBody` gains a nested plain struct `CharacterState state` (a `GroundState` enum of `uint8_t`, two entities, five `Vec3`s, two bools), read in C# straight from the component with no native call; physics overwrites it at each write-back. No new events: characters use `ContactBegin`/`ContactEnd` and `TriggerEnter`/`TriggerExit`, already readable through `EventReader<T>`. `ForEachContact` on a character must work from C# as it does for bodies. Landing and coyote time are game code (a C# system keeps last tick's `state.ground`).
