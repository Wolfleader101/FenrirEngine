# What does the physics scripting extension give C#?

Type: grilling
Status: open
Blocked by: 04, 08, 09, 16, 18

## Question

What does FenrirPhysics's scripting extension (ADR-0009) give C# code?

- Which components C# sees (body, colliders) and which fields are writable from C#.
- Which native functions: queries, applying forces and impulses, setting velocities, teleporting a body.
- Which events: the contact and trigger events from "How do contact and trigger events reach game code?".
- Native functions may read and write component data but make no structural changes (ADR-0009): which physics operations does that rule out, and is that acceptable?
- Where the C# contract project lives, and how it is built only when scripting is enabled.
