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
- No C# code stamps an owner: every entity a script holds came from C++ (a native's out parameter, a component field, a query hit) with its list set. The constructor from a value is `internal`, for tests that name an entity by id, and gives owner 0.
- `default(Entity)` is value 0 with no scene, as `default(Joint)` is today: a zeroed link C++ reads as not valid. The doc comment says so.
- A component field is then `public Entity BodyA = new();` in a writable mirror and `public readonly Entity BodyA = new();` in a read-only one, with no conversion, projection or second type: `joint.BodyA = door`, `if (contact.BodyB == Entity.None)`, `foreach` over `Relationship` by `.FirstChild` and `.NextSibling`.

### `EntityValue` goes: natives take the entity

`Fenrir::EntityValue` existed so a native could take or return an entity as its 32-bit value while C# `Entity` was that value. With C# `Entity` the mirror of `Fenrir::Entity`, a native takes the entity itself: a `const Fenrir::Entity*` parameter is `in Entity` in C# (the generator's pointer rule), and a native that makes one returns it through a `@param[out] Fenrir::Entity*`, which C# calls with `out Entity`. C++ reads the owner off the entity it is given and stamps it on the entity it returns, so no C# code ever stamps an owner: `Native.EntityLinkOwner` goes, and the constructor from a value is `internal`, for tests that name an entity by id. `NativeGuard::Alive` takes a `const Entity&` and may also check that its owner is the active scene's list (a stale link from another scene is not alive). `EntityValue.hpp`, `EntityValueMapping.hpp`, the bound-type entry in `FenrirScripting.binding-defaults.toml` and every selection's `"Fenrir::EntityValue" = "bound-type"` line are deleted; the generator needs no new rule.

The natives that take an `EntityValue` today, by file: `FenrirPhysics/Scripting/src/PhysicsScripting.cpp` (20 uses), `FenrirRenderer/Scripting/src/RendererScripting.cpp` (12), `FenrirDebugDraw/Scripting/src/DebugDrawScriptingState.hpp` and `PhysicsScriptingState.hpp`, `RendererScriptingState.hpp` (2 each), with their tests (`Rules.cpp`, `NoOps.cpp`) and `FenrirScripting/Tests/ScriptingTests.cpp`'s guard test. The built-in natives in `FenrirScripting/src/Bindings.cpp` take `std::uint32_t value` today and move to `const Fenrir::Entity*` the same way, `EntityCreate` returning its entity through an out pointer. Ticket 01 already moved every struct that held an `EntityValue` to hold a `Fenrir::Entity`.

### Scripts, tests, docs

- The scripting test fixture `packages/FenrirScripting/Tests/Fixture/Links.cs` (`Tether.Target` is `Entity`; `Unlink` assigns `Entity.None`; `ReadableMembers` pins `Entity`'s public surface instead) and `Registry.cs` (the hierarchy reads compare with `Entity.None`); a case pins that writing `Entity.None` stores owner 0 and that a native-returned entity stores the active scene's list.
- The physics scripting fixture and the examples (`examples/PhysicsDemo/scripts/*.cs`, `examples/RendererDemo/scripts/MeshesAndMaterials.cs`) lose every `EntityLink` spelling. The user checks the examples; sessions leave DemoApp alone.
- `GLOSSARY.md`: "Entity" says a C# `Entity` is the same shape as a C++ one, value and owner, and the "Entity link" entry goes. ADR-0044's note that a component's entity crosses as an `EntityLink` is corrected in place.
- The physics compile-fail tests are untouched: the state structs do not change.

### Could `Entity` be generated whole?

The layout half is. The API half is hand-written because a description cannot say it: `Add<T>`, `Get<T>`, `Has<T>` and `Remove<T>` are generics over component ids, and equality, `None` and `ToString` are C#'s. The generator's facade parts (`@facade Class.Method` on a native, README "Facade parts") generated static methods on a `partial class`, never on a mirror, and never instance methods. Ticket 03 (optional, after the rest) extends them to mirrors with one rule and no new mark syntax: a mark naming a struct the binding mirrors puts its method in the mirror's generated file, an instance method passing `in this` when the native's first parameter is `in` the mirror (natives take the entity by pointer, so the parameter itself says the method is on `this`; no field is named), a static one otherwise. What it generates for `Entity` is what passes a native's result through: `TryCreate`, `TryGetWorldTransform` and `TrySetWorldTransform`, over which the hand-written `Create`, `WorldTransform` and `SetWorldTransform` keep their throws. `Destroy` (a `ChangeError` turned into an exception, as `Add<T>` and `Remove<T>` are) and `IsValid` (a property) stay hand-written: a generated method of either name would change the script-facing API.

### Rejected: changing what the engine stores

Two engine-side ways to a 4-byte stored reference were considered and rejected on 2026-10-28. **B**, `Fenrir::Entity` drops its pointer and finds its list through a thread-local current list: a hidden global behind 1,755 call sites, wrong for an entity of an inactive scene. **C**, components store a 4-byte `EntityId` resolved through the list in hand: every read of a stored reference becomes `entities.GetEntity(joint.bodyA)`, which is what the entity handle exists to avoid. The engine keeps `Entity` as it is; C# mirrors it.

### Out of scope

- Natives taking a `Fenrir::Entity` by value (16 bytes) rather than by pointer: the pointer rule exists and costs nothing measurable.

## Decisions

1. One `Entity` in C#, the generated mirror of `Fenrir::Entity`, 16 bytes, with today's API as its hand-written partial; `EntityLink` is deleted.
2. The engine's `Entity` keeps its list pointer and API; C# mirrors the shape instead of inventing a storage type.
3. `EntityValue` is deleted: natives take `const Fenrir::Entity*` (`in Entity`) and return an entity through an out pointer. Physics binds the engine's query structs and drops its shadow copy; DebugDraw's options hold an `Entity`.
4. Equality by value; `Entity.None` is `{uint.MaxValue, 0}`; C++ stamps every owner, C# never does, and `Native.EntityLinkOwner` goes.
5. No generator change: the pointer rules already give `in Entity` and `out Entity`.
6. No ADR: ADR-0044's rule stands; its `EntityLink` sentence is corrected. The glossary's "Entity link" entry goes.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | Physics binds the engine's query structs (no shadow copy, no `exclude`, no conversions); DebugDraw's options hold `Fenrir::Entity` | none |
| 02 | `Entity` is the mirror: the built-in binding, `Entity.cs`, `EntityLink.cs` deleted; natives take the entity and `EntityValue` is deleted; the scripting tests, glossary, ADR-0044 note; everything regenerated; the examples' scripts | 01 |
| 03 | Optional: facade parts on a mirror generate `Entity`'s native-backed members | 02 |

A stack. 01 landed first (PR #202) so that no struct holds an `EntityValue` when 02 deletes it; its migrated structs show `EntityLink` through `uses` until 02 renames it. Branch `entity-fields-01` from this spec's branch, each later ticket from the previous ticket's pushed branch.

## Done looks like

- `grep -rn "EntityLink" packages examples` finds nothing.
- `grep -rn "EntityValue\|EntityLinkOwner" packages examples` finds nothing; `ls packages/FenrirPhysics/Scripting/src/ScriptQueries.hpp` fails.
- `grep -n "exclude" packages/FenrirPhysics/Scripting/api/FenrirPhysics.binding.toml` finds nothing.
- Every scripting test passes under the msvc-x64-debug preset, every physics compile-fail case included; both generators are no-ops; Iris.Bindings' tests pass.
