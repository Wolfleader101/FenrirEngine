# What does C# get for characters?

Type: grilling
Status: resolved
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

## Answer

**C# gets characters with no native move or state function.** It gets one writable component whose state is read-only in C#, the three motion tests as native functions, and `QueryFilter.Layers`. Everything else is doc wording on what already exists. No ADR: every call mirrors a decision already recorded (ADR-0009, ADR-0014, tickets 32–36), and a read-only `State` in C# is easy to reverse. Glossary: **character body** amended ("game code, in C++ or C#, sets the velocity").

**Facts found in the code** (on `dotnet-scripting`):
- C#'s `QueryFilter` is `Mask, IgnoreBody (Entity), HitTriggers`.
- C#'s name for `ForEachContact` is `PhysicsWorld.ContactsOf(Entity, Span<Contact>)`.
- `Entity.Get<T>()` and `Has<T>()` exist, so reading the ground collider's `PhysicsMaterial` needs nothing new.
- Writable `EntityLink` (`.None`, made from an `Entity`) landed with joints. Entities inside native components map through `EntityLinkView` stand-ins.
- `PhysicsWorld.Gravity` is readable.
- The event and `ContactEvents` doc comments say "rigid body entity".
- `ComponentFieldsGenerator` gives a `readonly` field of a writable component an accessor only on `ReadOnlyRef<T>`, none on `Ref<T>`.

### Decisions

1. **The C# component.** `CharacterBody` is `[NativeComponent("Fenrir.Physics.CharacterBody")] public struct CharacterBody : IComponent`. Its fields follow C++'s order in PascalCase, with C++'s defaults:
   - `Vector3 Velocity`;
   - `float MaxSlope = 0.785f`, `StepHeight = 0.4f`, `FloorSnap = 0.5f`, `PushForce = 100`, `Mass = 70`;
   - `bool StopOnSlope = true`, `Interpolate = true`, `Enabled = true`;
   - `public readonly CharacterState State`.

   `GroundState` is `enum GroundState : byte { InAir, OnGround, OnSteepGround, Touching }`.

   `CharacterState` is a `readonly struct` with `readonly` fields:
   - `GroundState Ground`;
   - `EntityLink GroundBody`, `GroundCollider` (`EntityLink.None` when new);
   - `Vector3 GroundPoint`, `GroundNormal`, `GroundVelocity`, `GroundAngularVelocity`, `MovedVelocity`, `CarriedVelocity`;
   - `bool OnWall`, `OnCeiling`, `ShapeBlocked`.

   C++ lets game code write the state and overwrites the write. C# turns that rule into a compile error, as `Relationship`'s links are read-only. Replacing the whole component (`body.Value = new CharacterBody { ... }`) writes a fresh state, which physics overwrites anyway.
   - There are no convenience members such as `IsGrounded`: C# matches C++.
   - The state's bools are read in place and `CharacterState` never crosses from C++ by value, so the README's caution about bools does not apply.
2. **FenrirScripting prerequisite: read-only fields readable through `Ref<T>`.** For a `readonly` field of a writable component, `ComponentFieldsGenerator` also emits a `ref readonly` accessor on `Ref<T>`. A controller holds `Ref<CharacterBody>` to write `Velocity`, so it can then read `body.State.Ground` but not write it. A generator test covers this. The spec lists it as an explicit prerequisite, as the joints spec listed `EntityLink`, and any later component with a read-only part gets it for free.
   - Rejected: a writable `State` as in C++, which loses the guard.
   - Rejected: documenting `body.Value.State` as the way to read it.
3. **`default(CharacterBody)`** is all zeros: a disabled character with every rule off, and no warning. The component's doc comment says to make it with `new`. A test checks that `new CharacterBody()` equals C++'s `CharacterBody{}`, including the empty ground links. No extra warning is added.
4. **Moving needs no native function** (ADR-0014). A C# `Tick` system:
   - writes `Velocity` through `Ref<CharacterBody>`;
   - adds `PhysicsWorld.Gravity * dt` itself and jumps by setting `Velocity.Y`;
   - teleports by writing the `Transform` position;
   - crouches by writing `Collider.Shape` once.

   The C# doc comments carry the behaviour notes that earlier tickets assigned here:
   - **Sliding:** add input to the `Velocity` physics wrote back rather than overwriting it, or a `StopOnSlope = false` slide only creeps (spike lesson 4).
   - **Being pushed:** dynamic bodies never push a character, and kinematic bodies shove it at their own speed into `Velocity`.
   - **Crouching:** on ground a crouch keeps the feet down, and in the air it keeps the head in place. `StepHeight` does not follow the shape, and `State.ShapeBlocked` stays true until the stand-up fits.
   - **Actual speed:** `State.MovedVelocity`, not `Velocity`, is how fast the character actually moved.
5. **State and events.** There is no native state query: C# reads `body.State` in place every tick. There are no new events: `ContactBegin`/`ContactEnd` and `TriggerEnter`/`TriggerExit` through `EventReader<T>` already name the character's entity.
   - `PhysicsWorld.ContactsOf(character, span)` works for characters. Its doc comment says a character's contacts are its end-of-move contacts, from before the step. That is how a wall jump gets the wall's normal.
   - The C# doc comments of the four events, `Contact` and `ContactEvents` say "body entity (a rigid body or a character body)" where they now say "rigid body entity", as ticket 34 does for C++.
   - Landing and coyote time are game code: a C# system keeps last tick's `GroundState` in a component or field of its own.
6. **The motion tests are native functions** in the foundation's query pattern:

   ```csharp
   public static bool    MoveCast(CollisionShape shape, Transform pose, Vector3 motion, out MoveCastHit hit, QueryFilter? filter = null);
   public static int     Penetrations(CollisionShape shape, Transform pose, Span<PenetrationHit> hits, QueryFilter? filter = null);
   public static Vector3 Depenetrate(CollisionShape shape, Transform pose, QueryFilter? filter = null);
   ```

   - `MoveCastHit { Entity Body, Collider; Vector3 Point, Normal; float Distance }` and `PenetrationHit { Entity Body, Collider; Vector3 Point, Normal; float Depth }` are plain structs in `Queries.cs` with plain `Entity` fields, like `ShapeCastHit`.
   - Each function gets a `PhysicsNative` entry. `Depenetrate`'s is `bool Depenetrate(in shape, in pose, in filter, out Vector3 offset)`, shaped like `Gravity`; the public wrapper returns the vector.
   - `QueryFilter` gains `public uint Layers;` between `Mask` and `IgnoreBody`, matching C++'s layout. It is 0 when new, which ignores bodies' masks. Its native mirror and the fixture change with it.
7. **No sugar for filtering like a character.** In C# it is `new QueryFilter { Mask = cf.Mask, Layers = cf.Layers, IgnoreBody = me }`, no longer than in C++, and `entity.Get<CollisionFilter>()` gives `cf`. A doc example on `QueryFilter.Layers` and on `MoveCast` shows that line, and the two languages stay the same.
   - Rejected: a C#-only `QueryFilter.Like(Entity)`, which would also have to handle a character with no `CollisionFilter`.
8. **Robustness: only the foundation's rules apply.**
   - With no physics world, `MoveCast` returns `false`, `Penetrations` returns 0 and `Depenetrate` returns zero, each with the debug warning logged once per native site.
   - A `MeshShape` finds nothing, as with `ShapeCast`.
   - Disabled and inactive characters are invisible to queries, and their `State` reads as defaults (physics already warns once on inactivity).
   - Nothing throws or makes a structural change.

   These rules go in `PhysicsWorld`'s remarks.
9. **Module shape.**
   - **FenrirScripting core:** the generator change and its test (decision 2).
   - **`Fenrir.Physics` contract:**
     - a new `Characters.cs` with `CharacterBody`, `CharacterState` and `GroundState`;
     - `MoveCastHit`, `PenetrationHit` and `QueryFilter.Layers` in `Queries.cs`;
     - the three functions on `PhysicsWorld` and their `PhysicsNative` entries;
     - the doc wording in `Events.cs`, `Queries.cs` and `Components.cs`.
   - **`FenrirPhysicsScripting`:**
     - the `CharacterBody` mapping, with an `EntityLinkView` stand-in for each ground entity;
     - the `QueryFilter` mirror gaining `layers`;
     - the three native functions.

     FenrirPhysics itself still names nothing from Iris.
10. **Tests:** new `PhysicsScriptingTests` cases with a `Characters.cs` fixture.
    - `CharacterBody`, `MoveCastHit`, `PenetrationHit` and the new `QueryFilter` validate against their native layouts.
    - `new CharacterBody()` equals C++'s `CharacterBody{}`.
    - A C# system makes a character, adds gravity and lands it, and reads `State.Ground == OnGround` with `GroundBody` naming the floor.
    - Writing `Velocity` moves the character.
    - A crouch writes `Collider` once. `ShapeBlocked` is true under a box and clears once the box is gone.
    - `MoveCast`, `Penetrations` and `Depenetrate` match C++ for the same scene, and return false, 0 or zero with no world.
    - `Layers` filters as in C++.
    - `ContactBegin` read in C# names the character.
11. **What the C#-built scene must exercise** is in "What do the character demo scenes show?" (39), as a note.
