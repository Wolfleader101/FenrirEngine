Status: ready-for-agent

# One runtime component type in FenrirECS, for native and dynamic components alike

Origin: architecture review of Iris and FenrirScripting (2026-10-14), candidate 1. This applies ADR-0008: "a feature that needs the ECS to do something it cannot yet do adds a method to `FenrirECS`".

## Problem Statement

Code outside the ECS often needs to work with a component type it knows only at run time: "this kind of component, whatever it is". Scripting needs this today. The editor's inspector and scene serialization will need it next, since this branch is built on `editor`.

`FenrirECS` provides no such thing. It has two separate interfaces:
- Typed native access (`GetComponent<T>` and friends) plus `ComponentSelector::Native<T>()`.
- Dynamic access (`GetDynamic`, `HasDynamic`, `AddDynamic`, `RemoveDynamic`, `CountDynamic`, `ClearDynamic`) plus `ComponentSelector::Dynamic`.

So FenrirScripting built the missing concept itself. `ComponentRegistration` carries six function slots (get, has, add, remove, count, plus an optional selector) and a copied size and alignment. `MakeComponent<T>` fills them from the typed interface. `MakeScriptComponent` fills them from the dynamic one.

This causes four problems:
- An ECS concept lives in the scripting module. The next caller (the editor) would have to build it again.
- ECS knowledge leaks into scripting. Scripting has to know that `Entity(id, &list)` resets an entity's Transform, Name and Relationship, and avoid it with a `LiveEntity` helper.
- The behaviour of "get/has/add/remove/count a component by its runtime type" is only tested through the scripting test suite, which needs .NET.
- Two selector kinds and two sets of operations mean every caller handles native and dynamic types on separate paths.

## Solution

`FenrirECS` provides a **component type**: one cheap, copyable value that names a component type, native or dynamic.

`EntityList` gets one set of operations that take a component type:
- get, has, add, remove, count and clear;
- a query over any mix of component types.

A caller holding a component type does not know or care whether it is a C++ struct or bytes a script defined.

FenrirScripting's `ComponentRegistration` keeps only what is about scripting: the name C# uses, the C# type it is validated against, the read-only, required and script-defined flags, the layout validation, and the component type. App authors keep writing `MakeComponent<T, Mapping>(name)` and `Scripting::RegisterComponent<T>(name)` exactly as before.

## User Stories

1. As an engine developer, I want one value that names a component type whether it is native or dynamic, so that code working with runtime types has one path, not two.
2. As an engine developer, I want to make a component type from a C++ component type, so that native components can be used wherever a runtime type is expected.
3. As an engine developer, I want to make a component type from a `DynamicComponentType`, so that components a script defines can be used the same way.
4. As an engine developer, I want a component type to be cheap to copy and to hold no reference to any entity list, so that I can make it once and use it with every scene.
5. As an engine developer, I want to ask a component type for its size and alignment, so that I can copy component bytes without carrying the layout separately.
6. As an engine developer, I want to ask whether a component type is dynamic, so that the rare caller that must treat them differently can.
7. As an engine developer, I want to get an entity's component of a given type as a pointer, so that I can read and write it without knowing its C++ type.
8. As an engine developer, I want get to return null for a dead entity or an entity without the component, so that absence is a value and not a crash.
9. As an engine developer, I want has to report whether a live entity has the component, so that I can branch without fetching.
10. As an engine developer, I want get and has on a dead or absent entity to never create the entity or give it components, so that asking never changes the list.
11. As an engine developer, I want add to give a live entity a default component (default-constructed for native, zeroed for dynamic), so that a new component never holds garbage.
12. As an engine developer, I want add to keep the component an entity already has, so that adding twice is harmless.
13. As an engine developer, I want add to return false for a dead entity, an invalid dynamic type, or a dynamic name whose shape conflicts with the components already stored, so that misuse is reported, not hidden.
14. As an engine developer, I want remove to return false only for a dead entity, and true when the component was already absent, so that removing is idempotent.
15. As an engine developer, I want count to say how many entities have the component, in constant time, so that I can bail out of empty queries cheaply.
16. As an engine developer, I want clear to remove every component of a type from every entity and report how many there were, so that a script component whose layout changed can be dropped and re-added with its new shape.
17. As an engine developer, I want a query that takes any mix of component types and visits exactly the entities that have all of them, so that native and script components are queried together.
18. As an engine developer, I want a query with no component types to visit nothing, so that the empty case needs no special handling.
19. As an engine developer, I want a query naming a dynamic type no entity has to visit nothing and create no storage, so that querying for a script component never leaves traces in a scene.
20. As an engine developer, I want the documented costs of ADR-0008 kept: selecting or counting a native type may create its empty storage, but a dynamic one never does. Then the ADR stays true.
21. As an engine developer, I want the ECS's existing defences kept: a dynamic name whose storage id collides with another dynamic name or with a native storage is refused, not allowed to corrupt memory.
22. As an engine developer, I want pointers from get to stay valid while other entities are given components of the same type (removing one may move another, as today; C# defers removals until a system returns), so that a `Ref<T>` in C# stays good during a system (ADR-0007).
23. As an engine developer, I want the typed interface (`GetComponent<T>`, `View`, `Group`, `ForEach`) unchanged, so that native systems and the renderer are untouched.
24. As an engine developer, I want no EnTT type in the component type's interface, so that ADR-0008's rule holds for it.
25. As an editor developer, I want the component type in `FenrirECS` and not in scripting, so that the inspector and serializer can use it without depending on scripting or .NET.
26. As a scripting maintainer, I want `ComponentRegistration` to hold a component type instead of six function slots and a copied layout, so that the registration is only about what C# sees.
27. As a scripting maintainer, I want script-defined and native registrations to differ only in how their component type was made and in their flags, so that `MakeScriptComponent` and its duplicated operations disappear.
28. As a scripting maintainer, I want queries from C# to resolve component ids straight to component types, so that the query module stops checking for missing slots.
29. As a scripting maintainer, I want structural-change playback to add, get and remove through the component type, so that it no longer depends on how each registration was assembled.
30. As a scripting maintainer, I want read-only and required components still refused for add and remove by the scripting layer, with the same messages, so that what C# sees is unchanged.
31. As an app author, I want `MakeComponent<T, Mapping>`, `MakeReadOnlyComponent<T, Mapping>` and `Scripting::RegisterComponent<T>` to keep their signatures and meaning, so that my registration code does not change.
32. As an app author using `Scripting::FindComponent`, I want the found registration to give me its component type, so that I can add, get and remove that component on an entity through `EntityList` from native code.
33. As a maintainer, I want the component type's behaviour tested in `FenrirECSTests` without .NET, so that an ECS bug is found and fixed in the ECS.
34. As a maintainer, I want ADR-0008 and `GLOSSARY.md` to describe the component type once it exists, so that the documents stay true (CODING_STANDARDS).

## Implementation Decisions

### The component type (FenrirECS)

- A new public type `ComponentType` in `FenrirECS`, in its own public header. It is made with:
  - `ComponentType::Native<T>()`, for a default-constructible C++ component type;
  - `ComponentType::Dynamic(DynamicComponentType)`, which validates nothing until used, as `DynamicComponentType` does today.
- It reports `Size()`, `Alignment()` and `IsDynamic()`.
- It carries no name. Naming a component for C#, for an inspector or for a file is the caller's job. A dynamic type's name is part of its `DynamicComponentType`, which stays as the layout description of a dynamic type.
- It is a small value: cheap to copy, with no allocation per operation, and no reference to an `EntityList`.
  - The native case is a per-`T` set of plain function pointers. These are instantiated in the header, the same way `ComponentSelector::Native<T>` is today; ADR-0008 already accepts this compile-time dependency.
  - The dynamic case holds the `DynamicComponentType`.
- Equality and hashing of component types are not provided.

### What EntityList provides

These operations take a `const ComponentType&`:
- `Get(type, entity)` returns `void*` or null.
- `Has(type, entity)`.
- `Add(type, entity)`: a default component, keeping one already present.
- `Remove(type, entity)`: false only for a dead entity.
- `Count(type)`: constant time.
- `Clear(type)`: returns how many were removed. Afterwards a dynamic name may be added with another shape.
- `ForEachWith(std::span<const ComponentType>, func)`.

Their failure and storage rules are exactly today's rules for the dynamic operations and for `ComponentSelector`/`CountOf`, extended to native types as in the user stories:
- Get and has never create entities or give default components.
- A dynamic type never creates storage until it is added.
- A native type may create empty storage on count or select.

### Removed from FenrirECS's public interface

- `ComponentSelector`
- `CountOf`
- `GetDynamic`, `HasDynamic`, `AddDynamic`, `RemoveDynamic`, `CountDynamic`, `ClearDynamic`

No caller outside `FenrirECS` and its tests uses them except FenrirScripting, which moves to `ComponentType` in the same change. `DynamicComponentType`, `ByteStorage`, the storage-id collision defence, `ReserveEntity`/`CompleteEntity` and the typed templates are unchanged.

### Scripting

- `ComponentRegistration` replaces `get`, `has`, `add`, `remove`, `count`, `selector`, `size` and `alignment` with one `ComponentType type`. It keeps `name`, `typeName`, `readOnly`, `required`, `scriptDefined` and `validate`.
- `MakeComponent` and `MakeReadOnlyComponent` keep their signatures, and build the registration from `ComponentType::Native<T>()`. `MakeReadOnlyComponent` sets only the flag.
- The `Detail::LiveEntity` helper goes, since the ECS owns that rule now.
- `MakeScriptComponent` goes. A script-defined registration is built from `ComponentType::Dynamic` plus its flags where the schema reconciles a generation.
- Read-only and required components are refused at the scripting layer, using the flags, wherever add or remove is recorded or played back. No refusal may depend on an absent function slot any more. Messages, ordering and skip behaviour are unchanged.
- Queries resolve each id to its registration's component type, and pass the span of component types to `ForEachWith`. The "smallest pool first" early-out uses `Count`.
- Change playback copies an added value using `type.Size()`.
- The schema's clear-on-layout-change uses `Clear(ComponentType::Dynamic(...))` on every scene.
- Under ADR-0008, `ScriptingDoesNotUseEnTT` must still pass.

### Documents changed in the same change

- ADR-0008: the "What `FenrirECS` provides instead" list and the "Cost" section name `ComponentType` and its operations instead of `ComponentSelector`, `CountOf` and the `*Dynamic` methods. The decision itself does not change.
- `GLOSSARY.md` gains **Component type**: a value naming one kind of component, native or dynamic, that the ECS's runtime operations and queries take. *Avoid:* selector, registration (registration is scripting's record of what C# sees).
- `packages/FenrirScripting/README.md`: update anything describing the registration's operations.

## Testing Decisions

- **One seam: `FenrirECS`'s public interface,** exercised by `FenrirECSTests` (Catch2, built with `FENRIR_BUILD_TESTS`, no .NET). No new scripting tests are written for this change.
- **What makes a good test here:** it makes component types and an `EntityList` through public headers only, does something a caller would do, and asserts what a caller can observe:
  - return values;
  - pointers and the bytes behind them;
  - which entities a query visits;
  - `ComponentStorageCount` for "made no storage".

  It never names EnTT, and never reaches into storage. Expected values are literals or worked examples. One behaviour per `TEST_CASE`, named for the behaviour, with an `INFO` on every `REQUIRE` (CODING_STANDARDS).
- **Behaviour the type contract promises for both kinds** is written once and run for a native type and a dynamic type of the same layout (a Catch2 template test or a shared helper). Then "native and dynamic behave the same" is itself what is tested:
  - get, has, add-keeps-existing, remove-idempotent, dead-entity refusals, count, clear, size and alignment.
- **Cases for one kind only:**
  - Native add default-constructs (a type with a non-zero default member initialiser shows it).
  - Get and has on a dead entity do not give it Transform, Name or Relationship.
  - Dynamic add zeroes.
  - The dynamic shape-conflict and storage-id-collision refusals.
  - Dynamic queries and counts make no storage; native ones may.
  - Pointers to dynamic components do not move while other entities add and remove.
  - Mixed native-and-dynamic queries visit exactly the right entities.
- **Prior art:** the existing `FenrirECSTests` cases in `DynamicComponents.cpp` and `Queries.cpp`. They move to the new interface with their expectations unchanged, which shows the dynamic behaviour is preserved.
- **Scripting tests:**
  - The cases that test only ECS behaviour are deleted. Their behaviour is now owned and tested by `FenrirECSTests`:
    - "a runtime view over native and script pools iterates the right entities";
    - "script component storage honours alignment and never moves elements";
    - "the ECS holds dynamic components and refuses to change their shape while in use".
  - Other scripting cases that call a registration's slots directly are rewritten to use its component type with `EntityList`, without changing what they assert.
  - The scripting suite is not extended for this change, but it must still pass.

## Out of Scope

- Events and the App's named queues (review candidate 3).
- Letting other engine modules expose functions to C# (review candidate 2).
- Names, a registry of component types by name, equality or hashing in `FenrirECS`.
- The editor inspector and scene serialization themselves. This only makes them possible.
- Removing EnTT from `FenrirECS`'s public headers (ADR-0008, "What would remove it").
- DemoApp's existing direct EnTT use.
- Any change to C#, the scripting contract, the source generators, or Iris.

## Further Notes

- The behaviour C# sees must not change. The rule "a refactor leaves behaviour, messages and ordering unchanged" applies to everything on the scripting side.
- Do it before review candidates 2 and 3. Both get simpler once scripting registrations are only about scripting.
