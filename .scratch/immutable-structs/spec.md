Status: ready-for-agent

# Immutable structs: a read-only mirror C# assigns whole, so physics uses the built-in binding

Origin: the built-in-extension spec's ticket 03 (PR #189, 2026-10-28) left one documented gap: FenrirPhysics could not `uses` the built-in binding and kept its own `[bound-types]` table for `Fenrir::Transform` and `Fenrir::Entity`. The gap was traced on 2026-10-28 to one Iris.Bindings rule; this spec closes it with one ticket.

The vocabulary is the repo's: *binding*, *mirror*, *bound type*, *API description*, *shared struct* from `GLOSSARY.md`; *interface*, *locality* from the codebase-design skill.

## Problem statement

Iris.Bindings' `readonly-structs` key serves two intents with one rule. The intent it was written for is a **C++-owned copy C# only reads**: physics's `CharacterState`, `VehicleState` and `VehicleWheelState`, input's claims. For those the mirror is a `readonly struct` and, by the rule in `CSharpWriter.Fields` ("a field holding a read-only struct is read-only too, so C# cannot write the struct whole"), a writable component holding one gets a `readonly` field: `CharacterBody.State` cannot be assigned, which the physics compile-fail tests pin.

The built-in binding also lists `Fenrir::Entity`, for a different reason: an `EntityLink` is an **immutable value**. Scripts never write its fields, but they assign it whole all the time (`joint.BodyA = post`). Under the one rule, any writable mirror holding the built-in `EntityLink` would get a `readonly` field, so physics could not hold it as a used mirror. It held `Entity` (and `Transform`, for symmetry) as bound types instead, which meant describing both in its own package selection (`packages/FenrirPhysics/api/FenrirPhysics.api.toml`, `"Fenrir::Entity" = "bound-type"`) while FenrirECS describes them as shared structs. Two descriptions of one type is exactly what `uses` exists to remove, and the duplicate is what refuses the `uses`: with the table kept, "`Fenrir::Entity` is mirrored by the binding it uses but `[bound-types]` names it too"; with it dropped, "selects it as a bound type and FenrirECS.api.json as a shared struct".

## Solution

### Two keys for two intents

`[csharp]` gains `immutable-structs`, beside `readonly-structs`. A struct in either is mirrored as a `readonly struct` of `readonly` fields with its constructor, as today. The holder-field rule applies to `readonly-structs` only: a field holding an `immutable-structs` mirror is as writable as its holder. A type in both lists fails naming both keys; an entry that is not a described struct fails as `readonly-structs`' does. `Binding.IsReadOnly` keeps answering for the mirror's own shape (both lists); a new `Binding.HoldersAreReadOnly(type)` answers for the field rule (`readonly-structs` and read-only registered resources only, through `uses` as `IsReadOnly` is). The README's sentence splits in two and the configuration listing shows the key.

### The built-in binding

`FenrirScripting.binding.toml` moves `Fenrir::Entity` from `readonly-structs` to `immutable-structs`; `Fenrir::Relationship` stays. Nothing generated changes: `EntityLink.cs` is the same `readonly partial struct`, and the one built-in mirror holding it, `Relationship`, is read-only itself.

### Physics uses the built-in binding

- `packages/FenrirPhysics/api/FenrirPhysics.api.toml` drops `"Fenrir::Transform" = "bound-type"` and `"Fenrir::Entity" = "bound-type"`; `fenrir-describe-api` then records `Entity` as `nested` in `FenrirPhysics.api.json`, as the other packages' descriptions record the types they hold from elsewhere. Two things found while building: `Entity` keeps its value and entity list private, and describe-api honoured `private-members` for selected types only, so the tool now lets `private-members` name a nested type a selected type holds (refusing one that is neither selected nor held), and the physics selection says `private-members = ["Fenrir::Entity"]`; and no physics type holds a `Transform` (only its natives take one, named through the built-in binding), so `Transform` leaves the physics description rather than becoming `nested`.
- `packages/FenrirPhysics/Scripting/api/FenrirPhysics.binding.toml` adds `"../../../FenrirScripting/api/FenrirScripting.binding.toml"` to `uses` and deletes its `[bound-types]` table and the comment above it. Its header comment stops saying it binds the two itself.
- `fenrir-generate-bindings`: the physics mirrors hold the built-in binding's `Transform` and `EntityLink` as used mirrors, with the `using` the generator adds. The one expected text change is each `= EntityLink.None` initialiser becoming `= new()`: `Entity`'s described default is `0xFFFFFFFF`, the generated constructor sets it, and the built-in `Relationship` already spells its links `= new()`. The defaults tests prove the bytes. The physics mappings header loses its two bound-type asserts and includes the built-in mappings header through `uses`. Every other generated line of physics is byte-identical; any that is not stops the ticket and is reported.
- The physics compile-fail tests (`CharacterStateWrite`, `VehicleStateWrite`, `WheelStateWrite` and their field variants) still fail to compile as they must: those three states stay in `readonly-structs`.

### Out of scope

- The `"Fenrir::EntityValue" = "bound-type"` lines DebugDraw's and physics's own selections keep. Two option structs default a field to an `EntityValue(...)` constructor call, which describe-api records only for a selected bound type; a selection declaring which types it treats as bound is a fair thing for a selection to say.
- A per-mirror default expression so the physics text would read `EntityLink.None` again: a key for spelling, when `new()` is the same value and the house style elsewhere.
- Any other binding's `readonly-structs` entries.

## Decisions

1. Split the intent, not the rule: `immutable-structs` is a second list, and `readonly-structs` keeps every behaviour it has.
2. `Fenrir::Entity` is immutable; `Relationship` and the physics and input state copies stay read-only.
3. Physics describes `Entity` as nested (naming it in `private-members`, which describe-api now allows for a nested type) and `uses` the built-in binding; its `[bound-types]` table goes, and `Transform` leaves its description.
4. `= new()` is accepted for the physics `EntityLink` initialisers.
5. The `EntityValue` selection lines stay.
6. No ADR (the decision is a configuration key with the existing rule's scope made precise); no glossary change.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `immutable-structs` in Iris.Bindings; the built-in and physics configurations; regeneration | none |

One ticket, cut from `dotnet-scripting`. Branch `immutable-structs-01`.

## Done looks like

- `grep -n "bound-types" packages/FenrirPhysics/Scripting/api/FenrirPhysics.binding.toml` finds nothing; `grep -n "bound-type" packages/FenrirPhysics/api/FenrirPhysics.api.toml` finds only the math binding's `Vec3` and `Quat`.
- `grep -rn '"Fenrir::Entity"' packages/*/api/*.api.toml packages/*/Scripting/api/*.api.toml` finds no selection outside FenrirECS's and the built-in binding's (physics's `private-members` line names it without selecting it).
- The physics compile-fail tests pass (the writes still fail to compile); every scripting test passes under the msvc-x64-debug preset; both generators are no-ops after the commit.
