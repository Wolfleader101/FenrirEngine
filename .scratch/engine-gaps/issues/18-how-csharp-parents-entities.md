# How does C# parent entities?

Type: grilling
Status: open
Blocked by: none
Settles: [C# cannot switch scenes, stop the app or parent entities](11-what-csharp-cannot-reach.md) (parenting)

## Question

`Relationship` is read-only from C#. What does C# get to make one entity the child of another and to undo it?

- The surface: a writable parent (`entity.Parent = other`), `SetParent`/`ClearParent` calls on a facade, or `Relationship` made writable as a component. How it maps onto FenrirScene's C++ parenting call.
- Whether the child keeps its world transform or its local transform when re-parented, and whether both are offered.
- When it takes effect: at once, or as a deferred structural change on the call context like inserts and removes (ADR-0029).
- What happens to children when a parent is destroyed, as C++ does it today.
- Whether entities spawned by `SpawnModel` can be re-parented under a game entity (a cage holding a friend, an effect following Kiki, a camera rig).

Outcome: a spec-ready design. This and [How does C# create, switch and destroy scenes, and stop the app?](19-how-csharp-reaches-scenes-and-the-app.md) can share one spec.

## Facts (checked 2026-11-19)

- `Relationship` is a generated `readonly struct : IReadOnlyComponent` (`packages/FenrirScripting/Managed/Fenrir.Scripting/Generated/Relationship.cs`), made read-only by `packages/FenrirScripting/api/FenrirScripting.binding.toml:19-20,38`.
- No reparent call exists in C#. `Entity.cs:107` sets a transform relative to the parent; `SpawnModel` takes a `parent` argument (`Entity.cs:129`).
- Bindings are generated: natives are doc-tagged C++ functions in `Scripting/src/*Scripting.cpp` with `@facade Class.Method` marks (`tools/describe-api/README.md:104`), written out by Iris.Bindings and `*.binding.toml`, with hand-written partial facades where a description is not enough (`packages/FenrirPhysics/Managed/Fenrir.Physics/PhysicsWorld.cs`).
