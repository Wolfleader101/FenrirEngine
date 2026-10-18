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

From "How does a character interact with other bodies?": `CharacterBody` gains two plain live floats, `pushForce` (N, default 100) and `mass` (kg, default 70), so it still maps directly. No push reading and no new events: what a character pushed or was hit by comes through `ContactBegin`/`ContactEnd` (contacts with dynamic bodies include the inner body's step contacts) and `ForEachContact`. Knockback and ice are game code writing `velocity` (ice reads `state.groundCollider`'s `PhysicsMaterial`); the C# docs should say that dynamic bodies never push a character and kinematic bodies shove it at their own speed into `velocity`. `QueryFilter.ignoreBody` takes a character's entity.

From "What are the motion-test and sweep queries, and how does a character change shape?": three new general `PhysicsWorld` queries, each a native function in the foundation's query pattern (C# `bool ... out hit` for the closest, `Span<T>` for many, `QueryFilter? filter = null`):
- `MoveCast(shape, pose, motion, filter)` returning a `MoveCastHit { body, collider, point, normal, distance }`, where `distance` is the safe distance (fixed private skin);
- `Penetrations(shape, pose, hits, filter)` writing `PenetrationHit { body, collider, point, normal, depth }`, deepest first;
- `Depenetrate(shape, pose, filter)` returning a `Vector3` offset.

`QueryFilter` gains `uint layers = 0` (0 ignores bodies' masks), so the C# struct changes layout for every query. Filtering a custom move like the character's own is `new QueryFilter { mask = cf.mask, layers = cf.layers, ignoreBody = character }`; whether C# gets sugar for that is this ticket's call. `CharacterState` gains `bool shapeBlocked`, still plain. Crouching from C# is writing the entity's `Collider` (the hand-written `CollisionShape` union) once; physics retries a stand-up each move until there is room, so C# never re-writes it each tick. The docs should say a crouch keeps the feet on the ground and the head in the air, and that `stepHeight` does not follow the shape.
