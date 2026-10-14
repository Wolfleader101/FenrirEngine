# 03: Remove `ComponentSelector`, `CountOf` and the `*Dynamic` methods

**What to build:** the contract step of the spec (`../spec.md`).

FenrirECS offers one way to work with a component type known at run time: `ComponentType` and its `EntityList` operations. The old parallel interface is deleted:
- `ComponentSelector`
- `CountOf`
- `GetDynamic`, `HasDynamic`, `AddDynamic`, `RemoveDynamic`, `CountDynamic`, `ClearDynamic`

`DynamicComponentType` stays as the layout description of a dynamic type. The typed templates (`GetComponent<T>`, `View`, `Group`, `ForEach`), `ReserveEntity`/`CompleteEntity` and the collision defence are unchanged. ADR-0008 is amended so it describes what FenrirECS now provides.

**Blocked by:** 02 (Move FenrirScripting onto `ComponentType`).

**Status:** resolved

- [x] `ComponentSelector`, `CountOf` and the six `*Dynamic` methods no longer exist in FenrirECS's public interface.
- [x] Nothing in the repository refers to them.
- [x] The old-interface `FenrirECSTests` cases are folded into the component-type tests with their expected values unchanged, or deleted where ticket 01's tests already pin the same behaviour. No behaviour they pinned loses coverage.
- [x] ADR-0008's "What `FenrirECS` provides instead" list and its "Cost" section name `ComponentType` and its operations instead of `ComponentSelector`, `CountOf` and the `*Dynamic` methods. The decision and the "what this does and does not buy" section are otherwise unchanged.
- [x] ADR-0008's "Cost" section says that, for a native type, any of the component-type operations (not only counting or selecting) may create its empty storage, which is what the code does since ticket 01.
- [x] With the `*Dynamic` methods gone, the per-operation `IsDynamic()` branch in `EntityList` is written once (review of ticket 01: Repeated Switches), not seven times.
- [x] No other doc (README, glossary, doc comments) still names the removed symbols.
- [x] All tests pass (FenrirECS and scripting), `ScriptingDoesNotUseEnTT` passes, and the DemoApp builds.

## Comments

Done. With the old methods gone, `EntityList` finds a component type's storage in two private functions, `FindStorage` and `StorageForAdding`, the only places the kinds differ; every operation and the query then work on that storage. A native `ComponentType` now carries just a function that finds its storage: EnTT's `push` default-constructs a native component and zeroes a dynamic one, and `value` reads either.

The old tests now use the new interface. Cases already pinned in ComponentTypes.cpp were dropped, and the rest stay in DynamicComponents.cpp (dynamic-only) and Queries.cpp (queries). Where each old case is now:
- validity, same id, zeroed on every add, reshaping once the last one is removed, largest component, separate names, clearing one leaves others: DynamicComponents.cpp
- native, dynamic, type order, nobody matches, a native query making its storage, changes between queries: Queries.cpp
- impossible types (including too large), nothing made for an unused type, add keeps the component (now also its address and a count of one), goes with its entity, alignment and no moving on add (with distinct addresses and the final count), shape conflict, colliding ids, clear, mixed query, no types, count: ComponentTypes.cpp

`ByteStorage::Find` and `Emplace`, which only the old methods called, are deleted.
