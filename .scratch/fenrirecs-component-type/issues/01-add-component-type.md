# 01: Add `ComponentType` and its `EntityList` operations beside the old ones

**What to build:** the expand step of the spec (`../spec.md`).

An engine developer can make a **component type** from a C++ component type (`ComponentType::Native<T>()`) or from a `DynamicComponentType` (`ComponentType::Dynamic(...)`). They can then use it with one set of `EntityList` operations, whichever kind it is:
- get, has, add, remove, count, clear;
- a `ForEachWith` query over any mix of component types.

The existing `ComponentSelector`, `CountOf` and `*Dynamic` methods stay as they are, so no caller changes and the whole build stays green.

The component type:
- is a cheap value, with no allocation per operation and no reference to an entity list;
- carries no name;
- reports its size and alignment, and whether it is dynamic;
- has no EnTT type in its interface (ADR-0008).

The native case is a per-`T` set of plain function pointers instantiated in the header. The dynamic case holds the `DynamicComponentType`.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] `ComponentType::Native<T>()` (for a default-constructible `T`) and `ComponentType::Dynamic(DynamicComponentType)` exist in FenrirECS's public headers. Each reports `Size()`, `Alignment()` and `IsDynamic()`.
- [x] `EntityList` provides get (pointer or null), has, add, remove, count, clear (returns how many were removed) and `ForEachWith(std::span<const ComponentType>, func)`, all taking a component type.
- [x] Get and has on a dead entity, or one without the component, never create the entity or give it Transform, Name or Relationship.
- [x] Add, for a live entity:
  - default-constructs a native component and zeroes a dynamic one;
  - keeps a component that is already there;
  - returns false for a dead entity, an invalid dynamic type, or a dynamic shape conflict.
- [x] Remove returns false only for a dead entity, and returns true when the component was already absent.
- [x] Count is constant time. Clear lets a dynamic name be added again with another shape.
- [x] Storage rules match ADR-0008:
  - a dynamic type never creates storage until it is added;
  - counting or selecting a native type may create its empty storage;
  - the storage-id collision defence still refuses dynamic-to-dynamic and dynamic-to-native clashes.
- [x] `ForEachWith`:
  - visits exactly the entities that have every listed component type, native, dynamic or mixed;
  - visits nothing when given no types;
  - visits nothing for a dynamic type no entity has.
- [x] `FenrirECSTests` (no .NET) covers all of the above through public headers only, with no EnTT named:
  - the shared behaviour (get, has, add-keeps, remove-idempotent, dead-entity refusals, count, clear, size and alignment) is written once and run for a native type and a dynamic type of the same layout;
  - the cases for one kind only are tested separately (native default construction, dynamic zeroing, shape conflict, collision, storage creation, pointer stability, mixed queries).
- [x] Every new `TEST_CASE` names one behaviour, and each `REQUIRE` has an `INFO` (CODING_STANDARDS).
- [x] `GLOSSARY.md` gains **Component type**: a value naming one kind of component, native or dynamic, that the ECS's runtime operations and queries take. *Avoid:* selector, registration.
- [x] Every existing test (FenrirECS and scripting) still passes, and `ScriptingDoesNotUseEnTT` still passes.

## Comments

Done. Dynamic-to-native storage-id clashes are still refused by the unchanged pool check, but no test builds one: doing so would name the ECS library's type hash. A comment above the collision test says so. `ComponentType::Native<T>` also refuses empty (tag) types, which have no bytes to point at.
