Status: ready-for-agent

# Registry table: one table behind the registry's kinds, and error codes on the generated pipeline

Origin: the architecture review of `dotnet-scripting` against `main` (2026-10-26), candidate 5 (one generic registry table, error codes generated across languages), held back until the generation-lifecycle stream (PRs #157 and #159) had landed, and grilled to fifteen decisions on 2026-10-27. The review's other candidates are built.

The vocabulary is the repo's: *component*, *event*, *native resource*, *shared struct*, *script report*, *script data*, *admission*, *API description*, *binding*, *native function* from `GLOSSARY.md`; *module*, *interface*, *seam*, *deep*, *shallow*, *locality*, *leverage* from the codebase-design skill.

## Problem statement

`packages/FenrirScripting/src/ComponentRegistry.hpp` is one name-to-id table written three times. Components, events and resources each have a map, a vector, their own `Resolve`, `Add`, `Find` and `Get`, and the same generation bump; shared structs are a fourth copy reduced to a name set. `RemoveScriptDefined` rebuilds two of the tables by hand. The callers repeat the pattern: `ScriptData.cpp` validates the four kinds with four loops that differ only in a noun and in whether `validate` may be empty, and `Scripting.cpp` re-checks at Install that a name is registered under one kind only, with a set of its own, because no table knows the others exist.

On the C# side `ComponentIds<T>`, `EventIds<T>` and `ResourceIds<T>` each implement the generation-cached lookup, and each site that needs an id throws its own copy of the three messages (registered for another type, no such name with an attribute, no such name without one).

The error codes are spelled three ways. `ChangeError` in `src/ChangeBuffer.hpp` is the C++ enum; `src/Bindings.cpp` restates its numbers in doc comments; `Entity.cs`, `Resources.cs` and `Events.cs` switch on literals (`-5 => throw ...`). The registry's resolve answers are not an enum at all: `-1` and `-3` in three functions. `-3` is `WrongType` from a resolve and `ReadOnlyComponent` from a change. Three of the change values (`UnknownComponent`, `UnknownEvent`, `UnknownResource`) and two more (`ReadOnlyComponent`, `ReadOnlyResource`) exist only to tell C# a noun the call site already knows.

The pipeline for the fix already exists: describe-api records an enum (negative `int32` enumerators included), Iris.Bindings writes a C# enum per described enum, and a native may return an enum by value (ADR-0044). FenrirScripting is the one package with no API description of its own, so its enums reach C# by hand.

## Solution

### `RegistryTable<Registration>`

One class template in `src/ComponentRegistry.hpp` (the file keeps its name: `ComponentRegistry` is still what the engine holds), constructed with the registry's generation counter and its noun:

- `Resolve(name, typeName)`: the id, or `RegistryError::NotRegistered`, or `RegistryError::WrongType` when the name is registered for another C# type.
- `Add(registration)`: the id, or `NotRegistered` when the name is empty or any table of the registry holds it.
- `AddAll(registrations)`: all or none, by the one rule `CanAddAll` states today (usable, not taken, not twice in the batch).
- `Find(name)`, `Get(id)`, `All()`.
- `RemoveWhere(predicate)`: drops every registration the predicate selects and re-ids what remains.
- `Noun()`: `"component"`, `"event"`, `"shared struct"`, `"resource"`.

Every mutation bumps the generation the table was given. `ComponentRegistry` becomes four tables, `Components()`, `Events()`, `SharedStructs()`, `Resources()`, the set of every name any table holds (the cross-kind rule that Install enforces today), the generation, and `RemoveScriptDefined`, which sweeps components and events with `RemoveWhere` since it is the one place that knows which tables a generation writes to. The per-kind methods (`ResolveEvent`, `AddResource`, `GetEvent`, `FindEvent`, `AddSharedStruct`...) are deleted, not kept as forwarders; callers write `registry.Events().Resolve(...)`. Shared structs get ids nobody asks for, in exchange for being one table like the others.

`Scripting.cpp`'s Install loses its name set and becomes one loop per extension over the four tables, with the message text (`"a " + table.Noun() + " in " + source + " has an empty or duplicate name"`) byte-identical. `ScriptData.cpp`'s four `ValidateAll*` functions become one template over a table that skips an empty `validate` and prefixes the diagnostic with the table's noun, as today.

### Two described enums

`RegistryError` and `ChangeError` move to a public header, `include/FenrirScripting/Errors.hpp`, in namespace `Fenrir`:

```cpp
enum class RegistryError : std::int32_t { NotRegistered = -1, WrongType = -2 };
enum class ChangeError : std::int32_t { None = 0, EntityNotAlive = -1, Unknown = -2, ReadOnly = -3, Required = -4, NotActive = -5 };
```

They are two because they answer different questions (a lookup, a refused change) from different natives; nothing compares across them, so the shared `-3` is gone and `WrongType` takes `-2` in its own enum. `ChangeError` loses its per-kind values: C# knows the noun from the call site, and the messages it throws do not change.

FenrirScripting gains `api/FenrirScripting.api.toml` selecting the two with role `enum`, a committed `api/FenrirScripting.api.json`, `fenrir_describe_api(FenrirScripting ...)` in its `CMakeLists.txt`, and the built-in binding's `iris_generate_binding` call passes both FenrirECS's and FenrirScripting's descriptions, as every package binding passes two. Iris.Bindings writes `Managed/Fenrir.Scripting/Generated/RegistryError.cs` and `ChangeError.cs`, public like every generated enum: a visibility key would be a generator change for no gain. No describe-api or Iris.Bindings change.

### What the built-in natives return

The change natives (`EntityDestroy`, `ComponentAdd`, `ComponentRemove`, `EventSend`, `ResourceInsert`, `ResourceRemove`) return `ChangeError` by value with `@notrunning Fenrir::ChangeError::NotActive`, so the generated C# declaration is typed and the numbered doc comments go. The three id natives keep returning `std::int32_t`: an id when non-negative, else a `RegistryError` value, which C# casts; an enum result would need an out parameter for the id. `EventReadAfter` returns `bool`. C# switches on names everywhere: no literal code remains in `Entity.cs`, `Resources.cs` or `Events.cs`.

### One C# cache

`RegistryIds<T, TKind>` in `Managed/Fenrir.Scripting/RegistryIds.cs`, with `TKind : IRegistryKind` (net10 static abstract members) supplying the attribute's name, the native to call and the noun, and `ComponentKind`, `EventKind`, `ResourceKind` as its three empty structs in the same file. It keeps the generation-cached `Id` and adds `Require()`, which throws the three existing messages with the kind's noun. `ComponentIds<T>`, `EventIds<T>` and `ResourceIds<T>` are deleted; `ComponentIds<T>.IsResource`, the one component-only fact, moves next to `Entity.IdOf`.

### Out of scope

- The package extensions' natives and their warnings (the native guard).
- Any change to describe-api or Iris.Bindings.
- A glossary term: *registry* already names the thing.

## Decisions (from the grilling)

1. One `RegistryTable<Registration>`, exposed as `Components()`, `Events()`, `SharedStructs()`, `Resources()`; the per-kind methods are deleted, not forwarded.
2. The table bumps the registry's generation itself, constructed with the counter.
3. Shared structs are a fourth table, their ids unused.
4. The registry owns the cross-kind "one name" rule; Install's set goes.
5. Each table carries its noun; Install's and ScriptData's loops become one each, messages unchanged.
6. Two enums, `RegistryError` and `ChangeError`; `WrongType` is `-2` in its own enum.
7. `ChangeError` collapses to `None, EntityNotAlive, Unknown, ReadOnly, Required, NotActive`.
8. Both live in `include/FenrirScripting/Errors.hpp`, described by a new `FenrirScripting.api.toml`, no tool change.
9. Change natives return `ChangeError`; id natives return an id or a `RegistryError` as `int32`; `EventReadAfter` returns `bool`.
10. `RegistryIds<T, TKind>` with `Require()` throwing the kind's messages; the three caches are deleted outright.
11. The enums are public in the contract.
12. `RemoveScriptDefined` stays on the registry.
13. Three stacked PRs into `dotnet-scripting`, branches `registry-table-01-table`, `-02-error-enums`, `-03-csharp-cache`.
14. Rules tests for the table; .NET tests only where a `ChangeError` value has no thrown-message test today; no CI grep for literals.
15. No new ADR; one amendment line on ADR-0044.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `RegistryTable`, the registry's cross-kind names and nouns, Install and ScriptData loops, Rules tests | none |
| 02 | The two enums in `Errors.hpp`, the selection and description, the regenerated binding, typed natives, C# on names, ADR-0044 line | 01 |
| 03 | `RegistryIds<T, TKind>` replacing the three C# caches | 02 |

01 and 02 share `src/ComponentRegistry.hpp`, `src/Bindings.cpp` and `src/ChangeBuffer.*`; 02 and 03 share `Entity.cs`, `Events.cs` and `Resources.cs`. They are one stack: each branch is cut from the previous one and each PR targets it.

## Done looks like

- `src/ComponentRegistry.hpp` has one table implementation; `grep -c "unordered_map<std::string, std::int32_t>"` finds one.
- `Scripting.cpp` keeps no name set of its own and `ScriptData.cpp` has one validate loop.
- Each error code has one C++ spelling (`Errors.hpp`) and one C# spelling (`Generated/*.cs`); `grep -n "=> \$\|== -" Entity.cs Resources.cs Events.cs` finds no literal code.
- `fenrir-generate-bindings` and `fenrir-describe-api` are no-ops after the committed regeneration.
- The msvc-x64-debug preset builds and tests locally; the other presets are CI's.
