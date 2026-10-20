# What does C# get for vehicles?

Type: grilling
Status: open
Blocked by: 42, 43, 44, 45

## Question

What does the physics scripting extension (`FenrirPhysicsScripting`, contract `Fenrir.Physics`) give C# for vehicles? Builds on [How is a vehicle modelled in the ECS?](42-vehicles-in-the-ecs.md), [Which settings does each vehicle kind expose?](43-vehicle-kinds-and-settings.md), [How does a vehicle step each tick, and what does game code read back?](44-how-a-vehicle-steps-and-reports.md) and [How does a vehicle meet other bodies, joints and characters?](45-vehicles-and-other-bodies.md), and follows "What does C# get for joints?" and "What does C# get for characters?".

- **Components:** the vehicle component (and wheel components, if wheels are entities) writable from C#; if the kind is a tagged union, whether it is hand-written like `CollisionShape` and `JointKind` or is the third union that triggers the `[TaggedUnion]` generator.
- **Arrays:** if wheels, gears or torque-curve points are arrays inside a component, how a C# component maps a fixed-size array (FenrirScripting may need a prerequisite, as read-only fields did for characters).
- **Driving:** driver input as field writes, so no native function.
- **State:** read-only in C# if written back (as `CharacterState` is), or native queries in the foundation's pattern.
- **Robustness:** the foundation's no-op-with-warning rules.
- **What the C# demo scene needs** that tests alone would not prove.

From "Which settings does each vehicle kind expose?": no vehicle component holds an array (curves are shape fields, gears are generated), so the **Arrays** bullet needs no FenrirScripting prerequisite. Two more components to map: `VehicleDifferential` and `VehicleAntiRollBar`, each naming two wheel entities. `VehicleInput` gains `gear`, `clutch`, `leftTrack` and `rightTrack` (track ratios default to 1).
