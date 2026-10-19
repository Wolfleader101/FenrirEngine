# 14: Characters in C#

**What to build:** C# controllers drive characters exactly as C++ does: write `Velocity` through `Ref<CharacterBody>`, read `State` through the same reference, with no native move or state call (spec: "FenrirPhysicsScripting and Fenrir.Physics"; ADR-0014, ADR-0009).

- **`Fenrir.Physics` gains `Characters.cs`:** `[NativeComponent("Fenrir.Physics.CharacterBody")] public struct CharacterBody : IComponent` with C++'s fields in order, in PascalCase, with C++'s defaults (`Vector3 Velocity`; `float MaxSlope = 0.785f, StepHeight = 0.4f, FloorSnap = 0.5f, PushForce = 100, Mass = 70`; `bool StopOnSlope = true, Interpolate = true, Enabled = true`; `public readonly CharacterState State`); `enum GroundState : byte`; `CharacterState` a `readonly struct` with `readonly` fields, `GroundBody` and `GroundCollider` as `EntityLink` (`EntityLink.None` from `new`).
- **Reading `State`** goes through ticket 01's `ref readonly` accessor on `Ref<T>`; writing it does not compile.
- **No native move or state function, no convenience members** such as `IsGrounded`. A C# `Tick` system writes `Velocity`, adds `PhysicsWorld.Gravity * dt`, jumps by setting `Velocity.Y`, teleports by writing the `Transform` position, crouches by writing `Collider.Shape` once.
- **Doc comments** carry the behaviour notes: sliding (add input to the written-back `Velocity`); being pushed (dynamic bodies never push, kinematic bodies shove at their own speed into `Velocity`); crouching (feet down on ground, head in place in the air, `StepHeight` does not follow, `State.ShapeBlocked` until the stand-up fits); actual speed (`State.MovedVelocity`). `PhysicsWorld.ContactsOf` says a character's contacts are its end-of-move contacts, from before the step. `default(CharacterBody)` is all zeros (a disabled character with every rule off, no warning) and its doc comment says to make it with `new`.
- **Wording:** "body entity (a rigid body or a character body)" in `Events.cs`, `Queries.cs` and `Components.cs`.
- **`FenrirPhysicsScripting`:** the `CharacterBody` mapping, with an `EntityLinkView` stand-in for each ground entity.

**Blocked by:** 01 (Read-only fields through Ref<T>); 10 (Shape changes and crouching); 11 (Character contact events, ForEachContact and determinism).

**Status:** resolved

- [x] `PhysicsScriptingTests` with a `Characters.cs` fixture: `CharacterBody` validates against its native layout; `new CharacterBody()` equals C++'s `CharacterBody{}`, including the empty ground links.
- [x] A C# system makes a character, adds gravity, lands it and reads `State.Ground == OnGround` with `GroundBody` naming the floor; writing `Velocity` moves it; writing `State` does not compile.
- [x] A crouch writes `Collider` once; `ShapeBlocked` is true under a box and clears once the box is gone.
- [x] `ContactBegin` read in C# names the character; `ContactsOf` lists its contacts.
- [x] Disabled and inactive characters' `State` reads as defaults; nothing throws.

**Resolved by:** branch claude/character-csharp-8b4a4a. The proof is `FenrirPhysicsScriptingTests`' `[characters]` cases, through the fixture's `Characters` class and its Tick system `CharacterController` (`Tests/Fixture/Characters.cs`), which adds `PhysicsWorld.Gravity * dt` and a test-set walk to every character's `Velocity`. `CharacterBody` maps through `Detail::CharacterBodyLayout` and `CharacterStateLayout` (each ground entity an `EntityLinkView`), so the mirror is checked as scripts load; a drifted one fails the load naming `State`. Writing `State` is refused at compile time by new `PhysicsCompile-*` CTest cases (`Tests/CompileFail`, run by FenrirScripting's `ExpectCompile.cmake`, which gained an optional `PROPERTY` for the physics contract's folder): `body.State = ...` is CS8331 and `body.State.Ground = ...` is CS0191, while reading it through `Ref<CharacterBody>` and `ReadOnlyRef<CharacterBody>` compiles. `ContactsOf` needed no native change: a character's entity has a `Collider`, so it already counts as having a body.
