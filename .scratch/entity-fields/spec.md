Status: ready-for-agent

# Entity fields: one C# `Entity`, the mirror of `Fenrir::Entity`, and no `EntityLink`

Origin: a question on the immutable-structs ticket (PR #199, 2026-10-28). Scripts name an entity inside a component through a second type, `EntityLink` (`joint.BodyA = door` works only through an implicit conversion, and reading it back is `joint.BodyA.Target`). The user wants component and event fields to read as `Entity`, as a Unity, Godot or s&box script would reference one, and no `EntityLink` at all. This spec makes the script-side `Entity` the generated mirror of C++ `Fenrir::Entity`, so there is one type in each language and they have the same shape.

The vocabulary is the repo's: *binding*, *mirror*, *bound type*, *native*, *shared struct* from `GLOSSARY.md`; *interface*, *locality* from the codebase-design skill.

## Problem statement

C++ `Fenrir::Entity` is 16 bytes: the 32-bit EnTT value and an `EntityList*`, which is what gives `entity.GetComponent<T>()` and the hierarchy calls their list without any context. A generated mirror is the C++ struct's bytes, so a component's entity field needs a 16-byte C# type. The script-side `Fenrir.Scripting.Entity` (`packages/FenrirScripting/Managed/Fenrir.Scripting/Entity.cs`) was written as the 4-byte value alone, with `Create`, `Destroy`, `Get<T>` and equality on it; natives take and return that value through the `Fenrir::EntityValue` bound type. So the built-in binding names the mirror of `Fenrir::Entity` `EntityLink` (`[names]` in `FenrirScripting.binding.toml`), and `EntityLink.cs` bridges the two by hand: `Target`, `IsNull`, `None`, and an implicit conversion from `Entity` that stamps the active scene's list.

The second type leaks storage into every script: `Joint.BodyA`, `Relationship.Parent`, `ContactBegin.BodyA` and `VehicleWheel.Chassis` are `EntityLink`, `contact.BodyA == player` does not compile, and the examples carry `NameOf(EntityLink)` beside `NameOf(Entity)`. It also shapes the extensions: physics keeps a shadow copy of its query structs with 32-bit entities (`packages/FenrirPhysics/Scripting/src/ScriptQueries.hpp`, its own API description, and conversions in every query native) and excludes the engine's own `QueryFilter`, the hits and `Contact` from its binding, because those hold a `Fenrir::Entity`; DebugDraw's script options hold an `EntityValue` for the same reason.

Surveyed 2026-10-28, for the engine-side alternatives this spec rejects (below): C++ `Entity`'s methods are called at about 1,755 sites; about 160 of those are on an entity read out of a component field; 47 `Entity` fields sit in shared headers; the app holds several scenes, each with its own `EntityList`.

## Solution

### `Entity` is the mirror

The built-in binding names the mirror of `Fenrir::Entity` `Entity`. The generated half is what `EntityLink`'s is today: a `readonly partial struct` (`immutable-structs`) with the two private fields `value` and `owner` (`private-fields`, `[names]`) and the constructor. The hand-written half is today's `Entity.cs` moved onto that partial: `Create`, `Destroy`, `Add<T>`, `Remove<T>`, `Has<T>`, `Get<T>`, `GetReadOnly<T>`, `IsValid`, the transform members, equality, `ToString`, `None`, and the constructor from a value. `EntityLink.cs` is deleted.

- A script-side `Entity` is 16 bytes, as a C++ one is. `Entity.None` is value `uint.MaxValue` and owner 0, which is what C++ `Entity{}` holds and what a component's `= new()` initialiser gives. Equality and the hash are by value: scripts see one scene.
- The constructor from a value (`new Entity(value)`, what every native result goes through) stamps the owner with the active scene's list through `Native.EntityLinkOwner`, as `EntityLink`'s conversion does today, except for `Entity.None`, which keeps owner 0. The native is a function-pointer call; caching the owner is an optimisation for later, if measured.
- `default(Entity)` is value 0 with no scene, as `default(Joint)` is today: a zeroed link C++ reads as not valid. The doc comment says so.
- A component field is then `public Entity BodyA = new();` in a writable mirror and `public readonly Entity BodyA = new();` in a read-only one, with no conversion, projection or second type: `joint.BodyA = door`, `if (contact.BodyB == Entity.None)`, `foreach` over `Relationship` by `.FirstChild` and `.NextSibling`.

### `EntityValue` is for natives only

`Fenrir::EntityValue` stays the bound type natives take and return the entity's value through (`wrapper = "u32"`, `constructor = ["value"]`, `type = "Entity"`): a native declared `std::uint32_t entity` or `EntityValue` is `Entity` in C#, passed as its value and wrapped on return. It can no longer be a field of a struct a binding mirrors, since C# `Entity` is 16 bytes and `EntityValue` 4: a struct holding one is refused by the generator, naming the field ("`<Type>::<field>` holds `Fenrir::EntityValue`, which is 4 bytes where C# `Entity` is 16: hold a `Fenrir::Entity`"). The generator's rule that a bound type's C# type may not be a mirror's name is relaxed for a bound type with a `wrapper`: such a type never has a layout of its own in C#.

Physics's query structs and DebugDraw's options therefore hold the engine's `Fenrir::Entity`:

- Physics binds the engine's own `QueryFilter`, `RaycastHit`, `ShapeCastHit`, `MoveCastHit`, `OverlapHit`, `PenetrationHit` and `Contact` (`packages/FenrirPhysics/include/FenrirPhysics/Queries.hpp`, already selected as shared structs in `FenrirPhysics.api.toml`): the `exclude` list in `FenrirPhysics.binding.toml` goes, `ScriptQueries.hpp` and `FenrirPhysicsScripting.api.toml`'s shadow types go, and the query natives in `PhysicsScripting.cpp` take and fill the engine's structs with no conversion. Scripts read `hit.Body` as an `Entity`, which they already spell.
- DebugDraw's `ScriptOptions.hpp` holds `Fenrir::Entity relativeTo` instead of `EntityValue`; its natives resolve it as they resolve any entity.

### Scripts, tests, docs

- The scripting test fixture `packages/FenrirScripting/Tests/Fixture/Links.cs` (`Tether.Target` is `Entity`; `Unlink` assigns `Entity.None`; `ReadableMembers` pins `Entity`'s public surface instead) and `Registry.cs` (the hierarchy reads compare with `Entity.None`); a case pins that writing `Entity.None` stores owner 0 and that a native-returned entity stores the active scene's list.
- The physics scripting fixture and the examples (`examples/PhysicsDemo/scripts/*.cs`, `examples/RendererDemo/scripts/MeshesAndMaterials.cs`) lose every `EntityLink` spelling. The user checks the examples; sessions leave DemoApp alone.
- `GLOSSARY.md`: "Entity" says a C# `Entity` is the same shape as a C++ one, value and owner, and the "Entity link" entry goes. ADR-0044's note that a component's entity crosses as an `EntityLink` is corrected in place.
- The physics compile-fail tests are untouched: the state structs do not change.

### Could `Entity` be generated whole?

The layout half is. The API half is hand-written because a description cannot say it: `Add<T>`, `Get<T>`, `Has<T>` and `Remove<T>` are generics over component ids, and equality, `None` and `ToString` are C#'s. The generator's facade parts (`@facade Class.Method` on a native, README "Facade parts") generate static methods on a `partial class`, never on a mirror, and never instance methods. Extending them to mirrors, so `@facade Entity.Create` and an instance form that passes a named field (`@facade Entity.Destroy(value)`) generate `Create`, `Destroy`, `IsValid`, `WorldTransform` and `SetWorldTransform` from `Bindings.cpp`, is ticket 03: optional, after the rest, because it removes five short methods and adds a generator rule.

### Rejected: changing what the engine stores

Two engine-side ways to a 4-byte stored reference were considered and rejected on 2026-10-28. **B**, `Fenrir::Entity` drops its pointer and finds its list through a thread-local current list: a hidden global behind 1,755 call sites, wrong for an entity of an inactive scene. **C**, components store a 4-byte `EntityId` resolved through the list in hand: every read of a stored reference becomes `entities.GetEntity(joint.bodyA)`, which is what the entity handle exists to avoid. The engine keeps `Entity` as it is; C# mirrors it.

### Out of scope

- Caching the active scene's owner on the C# side (a `static` refreshed by the host when the scene changes): measure first.
- Natives taking a `Fenrir::Entity` by value instead of its 32-bit value: `EntityValue` does the job and the natives are unchanged.

## Decisions

1. One `Entity` in C#, the generated mirror of `Fenrir::Entity`, 16 bytes, with today's API as its hand-written partial; `EntityLink` is deleted.
2. The engine's `Entity` keeps its list pointer and API; C# mirrors the shape instead of inventing a storage type.
3. `EntityValue` is the natives' wrapper only; a struct a binding mirrors holds `Fenrir::Entity`. Physics binds the engine's query structs and drops its shadow copy; DebugDraw's options hold an `Entity`.
4. Equality by value; `Entity.None` is `{uint.MaxValue, 0}`; a native-returned entity is stamped with the active scene's list.
5. The generator allows a wrapper bound type to name a mirror's C# type, and refuses `EntityValue` as a field.
6. No ADR: ADR-0044's rule stands; its `EntityLink` sentence is corrected. The glossary's "Entity link" entry goes.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | Physics binds the engine's query structs (no shadow copy, no `exclude`, no conversions); DebugDraw's options hold `Fenrir::Entity` | none |
| 02 | `Entity` is the mirror: the built-in binding, `Entity.cs`, `EntityLink.cs` deleted, the generator's two rules, the scripting tests, glossary, ADR-0044 note; everything regenerated; the examples' scripts | 01 |
| 03 | Optional: facade parts on a mirror generate `Entity`'s native-backed members | 02 |

A stack. 01 lands first because, under 02's rule, `EntityValue` may no longer be a field: in 01 the migrated structs hold `Fenrir::Entity`, which the physics and DebugDraw bindings show as `EntityLink` through `uses` until 02 renames it. Branch `entity-fields-01` from this spec's branch, each later ticket from the previous ticket's pushed branch.

## Done looks like

- `grep -rn "EntityLink" packages examples` finds nothing.
- `grep -rn "EntityValue" --include=*.hpp packages` finds only `EntityValue.hpp`, its mapping, and natives' parameters; `ls packages/FenrirPhysics/Scripting/src/ScriptQueries.hpp` fails.
- `grep -n "exclude" packages/FenrirPhysics/Scripting/api/FenrirPhysics.binding.toml` finds nothing.
- Every scripting test passes under the msvc-x64-debug preset, every physics compile-fail case included; both generators are no-ops; Iris.Bindings' tests pass.
