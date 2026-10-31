# What does C# get for ragdolls?

Type: grilling
Status: claimed
Blocked by: 51, 52, 53, 54

## Question

What does the scripting side get for ragdolls and collision groups, given ADR-0009 (packages extend scripting through extensions), ADR-0039 (C# reaches skeletons through mirrors and per-bone functions), ADR-0042 to ADR-0046 (the generated API description, natives passing enums and wrappers by value, bindings using other bindings' mirrors, a generated extension shape), the earlier families' rule that every physics component is writable from C# with C++'s defaults, and the decisions from [How do general collision groups work?](51-general-collision-groups.md), [How is a ragdoll modelled in the ECS, and where does the glue live?](52-ragdolls-in-the-ecs.md), [From what description is a ragdoll built, and with which bodies, joints, motors and strengths?](53-ragdoll-description-bodies-joints-and-motors.md) and [How does a ragdoll switch between following and simulating, and how is it powered?](54-ragdoll-switching-powering-and-following.md)?

- **Collision groups:** the component mirrored writable; a native only if physics hands out ids.
- **The glue's extension:** if the glue is a package, its own scripting extension and contract (`Fenrir.Ragdoll`), built only with physics, skeletons and scripting on, in the generated shape of ADR-0046; which mirrors (the ragdoll and body components, the description, state) and which natives (describe from a skeleton, build, switch mode, the whole-ragdoll operations such as an impulse).
- **Bone names in components:** `BoneName` is a fixed-size name (ADR-0037) that `BoneAttachment` already mirrors; the ragdoll body component reuses that mirror through the FenrirSkeleton binding (ADR-0044).
- **A description from C#:** whether a plain-data description with a variable number of parts can cross the boundary (components cannot hold a growable array and stay blittable, which is why wheels are entities), or whether C# builds a ragdoll by calling a native that derives it from the skeleton and then edits the spawned entities, or spawns body and joint entities itself and only adds the mapping components.
- **Read-back:** ragdoll state read-only in C# through `ref readonly`, as `CharacterState` and `VehicleState` are.
- **No-op rules:** the foundation's silent no-ops without a world or body, and ADR-0039's for a skeleton that is not ready.
- **Doc notes** for scripts: switching, partial ragdolls, strength, getting up.
- **The C# scene's list** handed to "What do the ragdoll demo scenes show?".

Probably no ADR.
