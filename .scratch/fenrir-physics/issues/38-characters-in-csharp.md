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
