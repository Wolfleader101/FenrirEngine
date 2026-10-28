Status: ready-for-agent

# Entity fields: scripts hold an `Entity`, and `EntityLink` is the storage behind it

Origin: a question on the immutable-structs ticket (PR #199, 2026-10-28). Scripts name an entity inside a component through a second type, `EntityLink` (`joint.BodyA = door` works only through an implicit conversion, and reading it back is `joint.BodyA.Target`). The user wants component and event fields to read as `Entity`, as a Unity, Godot or s&box script would reference one. This spec gives the generator a projection that does that and keeps `EntityLink` as storage.

The vocabulary is the repo's: *binding*, *mirror*, *entity link*, *shared struct* from `GLOSSARY.md`; *interface*, *locality* from the codebase-design skill.

## Problem statement

C++ `Fenrir::Entity` is 16 bytes: the 32-bit EnTT value and an `EntityList*` the engine keeps for itself, which is what lets C++ call `entity.GetComponent<T>()` on a link it finds in a component. A generated mirror is the C++ struct's bytes, laid out identically, so a component's entity field needs a 16-byte C# type. The script-side handle `Fenrir.Scripting.Entity` (`packages/FenrirScripting/Managed/Fenrir.Scripting/Entity.cs`) is the 4-byte value alone, with `Create`, `Destroy` and the component methods on it; it is what natives take and return (the `Fenrir::EntityValue` bound type). So the built-in binding names the mirror of `Fenrir::Entity` `EntityLink` (`[names]` in `FenrirScripting.binding.toml`), and `EntityLink.cs` bridges the two by hand: `Target` reads the `Entity` out, `IsNull`, `None`, and an implicit conversion from `Entity` that stamps the active scene's list.

The result leaks a storage detail into every script: `Joint.BodyA`, `Relationship.Parent`, `ContactBegin.BodyA`, `VehicleWheel.Chassis` and the rest are `EntityLink`, `contact.BodyA == player` does not compile, and the examples carry `NameOf(EntityLink link)` helpers beside `NameOf(Entity)`.

## Solution

### A projected field

Iris.Bindings gains a projection: a mirror the binding names in `[csharp] project-as` is stored as itself but shown as another C# type. For a field holding a projected mirror, `CSharpWriter.Fields` emits a private backing field of the mirror type and a public property of the projected type:

```csharp
/// <summary>A body entity: a RigidBody's, or a standalone static collider's. Never the world.</summary>
public Entity BodyA { get => bodyA.Target; set => bodyA = value; }
private EntityLink bodyA = new();
```

- The backing field is the member's C# name in camelCase (`bodyA`), with the initialiser the field would have had (`= new()`, or none when the mirror has no constructor). Layout is unchanged: a property adds no storage, so the mappings header's asserts and the defaults tests hold as they are.
- The field's doc comment goes on the property. The backing field gets none.
- On a read-only mirror (`readonly-events`, `readonly-structs`, `immutable-structs`, a read-only resource) the property has a getter only, and the backing field is `readonly`: `contact.BodyA` reads, and `contact.BodyA = x` fails as a `readonly` field would (CS0200).
- The projection's two expressions are configuration, not something the generator knows about links: `project-as` maps a described type to `{ type = "Entity", get = "Target", set = "value" }`, the getter reading the named member of the backing field and the setter assigning the projected value (through the mirror's implicit conversion) or a named static. Only the built-in binding sets it today, for `Fenrir::Entity`. A binding that `uses` one with a projection inherits it, as `IsReadOnly` is inherited.
- A projected mirror in an array (`[InlineArray]`) or a fixed buffer is refused with a diagnostic naming the field: nothing holds one today, and an indexer over a projected element is a later key if it is ever needed.
- A projected field cannot be passed by `ref` or `in`, since it is a property. Nothing in the repo does (`grep -rn "ref \w*\.\(BodyA\|Parent\|Target\)"` finds nothing); the README says so.
- The generator's `private-fields` list is unrelated: that makes a described field private for a hand-written part to read. A projected field is private because the property is its public face.

### `EntityLink` stays, as storage

`EntityLink` stays a public type: extension contracts (Fenrir.Physics) are other assemblies and hold it in their backing fields, so `internal` would need `InternalsVisibleTo` for every extension. Its hand-written part keeps `None`, `IsNull`, `Target` and the implicit conversion from `Entity`, so hand-written components and tests that hold one directly keep working. Its doc comment says what it is now: the storage of an entity field, which a generated mirror shows as `Entity`.

Two semantics are pinned in `EntityLink.cs`:

- Converting `Entity.None` gives `EntityLink.None` (value `uint.MaxValue`, owner 0), not a link with the active scene's list and no entity. C++'s default `Entity` has a null list, and a script writing `joint.BodyB = Entity.None` means "the world", as `new Joint()` holds. Today the conversion stamps the list regardless; the spec changes that, and a Links test pins it.
- Reading an empty link gives `Entity.None` (`Target` already does: the value is `uint.MaxValue`), so `if (contact.BodyB == Entity.None)` is how a script asks for the world. `IsNull` on the link is no longer in a script's path.

### The built-in binding, physics, scripts

- `FenrirScripting.binding.toml`: `project-as = { "Fenrir::Entity" = { type = "Entity", get = "Target" } }` beside `immutable-structs`. Regenerated: `Relationship` shows four `Entity` getters over four `readonly EntityLink` backing fields.
- Physics regenerates through `uses`: `Joint`, `CharacterState`, the five events, `VehicleWheel`, `VehicleDifferential`, `VehicleAntiRollBar` and `VehicleWheelState` show `Entity` properties. Physics's hand-written docs (`Joints.cs`, `Vehicles.cs`) stop naming `EntityLink.None`.
- The scripting test fixture `packages/FenrirScripting/Tests/Fixture/Links.cs` is the hand-written `Tether`, written in the projected form the generator emits, so the link tests (`LinkTo`, `Unlink`, `WriteZeroed`, `TargetOf`) test what scripts do: `tether.Target = Entity.None`. `ReadableMembers` keeps pinning `EntityLink`'s public surface. `Registry.cs`'s hierarchy reads drop `Link(in EntityLink)` for `.Parent == Entity.None ? uint.MaxValue : .Parent.Value`.
- The examples (`examples/PhysicsDemo/scripts/*.cs`, `examples/RendererDemo/scripts/MeshesAndMaterials.cs`) lose their `EntityLink` helpers and `EntityLink.None` spellings. The user checks the examples; sessions leave DemoApp alone.
- `GLOSSARY.md` "Entity link" says a script sees an entity field as an `Entity`, and `EntityLink` is how a mirror stores one.

### Removing `EntityLink` entirely: an engine decision (ticket 04)

The projection hides `EntityLink` from scripts but keeps it as storage, because the stored bytes are C++ `Fenrir::Entity`'s 16. Removing the type outright means a component stores 4 bytes, which is a change to the engine, not the generator. Surveyed 2026-10-28:

- C++ `Entity` is `entt::entity` plus `EntityList*`; the pointer serves `AddComponent`, `GetComponent`, `HasComponent`, `IsValid`, the hierarchy calls and `operator==` (which compares the list too). Those methods are called at about 1,755 sites across the packages and examples.
- About 160 of those sites call a method on an entity read out of a component or event field (`joint.bodyA.GetComponent<T>()`, `relationship.firstChild.IsValid()`): 6 in `FenrirPhysics/src`, 5 in `FenrirECS/src/Entity.cpp`, 4 in FenrirScripting's tests, the rest in physics's tests (about 100) and the PhysicsDemo (about 40).
- 47 `Entity` fields sit in shared headers: physics's `Events.hpp` (17), `Queries.hpp` (11), `Vehicles.hpp` (7), `PhysicsWorld.hpp` (4), `Joints.hpp` and `Characters.hpp` (2 each), DebugDraw (3), SpawnNodes (1), plus `Relationship`'s four in `Entity.hpp`.
- The app holds several scenes, each with its own `EntityList`; one is active. The link's owner is how C++ reads a script-written link as an entity of the active scene. FenrirECS's tests make local `EntityList`s.

Three ways to go:

- **A. Keep the projection (tickets 01 to 03).** No engine change; `EntityLink` stays as storage behind the property. Scripts never spell it.
- **B. `Fenrir::Entity` becomes 4 bytes and finds its list through context.** A current entity list (thread-local, set by a scene while its systems run and by an `EntityList` when it is made with none current) serves every method, so the 1,755 call sites stand; equality is by id. The mirror of `Fenrir::Entity` is then the 4-byte `Entity` itself: `EntityLink.cs`, `Native.EntityLinkOwner` and the projection all go, and tickets 01 to 03 are not needed. Costs: a hidden context the coding standards' locality rule weighs against; an entity of an inactive scene resolves against the wrong list unless the context is switched around it; parallel systems must run one scene per thread.
- **C. Components store a 4-byte `EntityId`; `Fenrir::Entity` keeps its pointer and its API.** A stored reference is the id alone, as `entt::entity` and a Unity instance id are; code reading one out of a component resolves it through the list in hand (`entities.GetEntity(joint.bodyA)`), which every system and test already has. The 47 fields retype and the 160 reading sites change, mostly physics tests and the demo; the engine's `Relationship` and `EntityList` hierarchy code go with them. The mirror of `EntityId` is `Entity` through a bound type, as `EntityValue` is today; `EntityLink.cs` and `Native.EntityLinkOwner` go, and tickets 01 to 03 are not needed. No hidden state; equality by id.

Recommendation: **C**. It removes the type the user does not want, keeps `entity.GetComponent<T>()` on the handles code holds, and makes "a stored entity reference is an id you resolve against the list you have" the one rule in both languages, with no global behind it. B is the smaller edit but trades the pointer for a hidden current list. A is what to keep only if the engine change is not wanted now. The decision is the user's, on ticket 04: choosing B or C marks tickets 01 to 03 `wontfix`, and ticket 01's session (started 2026-10-28) is stopped.

### Out of scope

- A projection for any type but `Fenrir::Entity`. The key is general so the rule has one shape, but no other type needs it.
- Natives: they take and return `Entity` through the `EntityValue` bound type already.

## Decisions

1. Project, do not retype: the stored bytes stay `EntityLink`; the generator shows `Entity`.
2. The projection is a generator key (`[csharp] project-as`), inherited through `uses`, with the getter and setter expressions in configuration.
3. `EntityLink` stays public, as storage, with its hand-written surface; converting `Entity.None` yields `EntityLink.None`.
4. A projected field is a property: getter and setter on a writable mirror, getter only on a read-only one; no `ref`/`in`, no arrays.
5. The built-in `Relationship`, physics's holders, the scripting test fixture's `Tether`, the examples and the glossary follow.
6. No ADR for the projection: ADR-0044's rule (bindings use other bindings' mirrors) is unchanged; the projection is a mirror's presentation. The glossary entry changes.
7. Open: whether `EntityLink` is removed outright by changing what a component stores (ticket 04, options B and C above). That one gets an ADR, since it changes what a stored entity reference is in C++.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `project-as` in Iris.Bindings: writer, validation, README, tests, fixture | none |
| 02 | The built-in binding projects `Fenrir::Entity`; `EntityLink.cs`'s `None` conversion; the scripting link tests and `Registry.cs`; the glossary | 01 |
| 03 | Physics regenerated; its hand-written docs; the examples' scripts | 02 |
| 04 | Remove `EntityLink`: decide B or C, then the engine change, the bindings and the scripts | none (a decision; supersedes 01 to 03 when taken) |

01 to 03 are a stack: 02's regenerated outputs need 01's generator, and 03's need 02's configuration. Branch `entity-fields-01` from this spec's branch, each later ticket from the previous ticket's pushed branch. 04 is the user's decision first; if B or C is chosen, 01 to 03 are marked `wontfix` and 04 is cut from `dotnet-scripting` on top of PR #199.

## Done looks like

- `grep -rn "EntityLink" packages/*/Managed/*/Generated/` finds only private backing fields.
- `grep -rn "EntityLink" examples/*/scripts/` finds nothing.
- `grep -rn "public EntityLink" packages/*/Managed/ packages/*/Tests/` finds nothing but `EntityLink.cs`'s own members.
- Every scripting test passes under the msvc-x64-debug preset, every physics compile-fail case included; both generators are no-ops; Iris.Bindings' tests pass.
