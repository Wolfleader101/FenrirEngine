# 03: Remove `ComponentSelector`, `CountOf` and the `*Dynamic` methods

**What to build:** the contract step of the spec (`../spec.md`).

FenrirECS offers one way to work with a component type known at run time: `ComponentType` and its `EntityList` operations. The old parallel interface is deleted:
- `ComponentSelector`
- `CountOf`
- `GetDynamic`, `HasDynamic`, `AddDynamic`, `RemoveDynamic`, `CountDynamic`, `ClearDynamic`

`DynamicComponentType` stays as the layout description of a dynamic type. The typed templates (`GetComponent<T>`, `View`, `Group`, `ForEach`), `ReserveEntity`/`CompleteEntity` and the collision defence are unchanged. ADR-0008 is amended so it describes what FenrirECS now provides.

**Blocked by:** 02 (Move FenrirScripting onto `ComponentType`).

**Status:** ready-for-agent

- [ ] `ComponentSelector`, `CountOf` and the six `*Dynamic` methods no longer exist in FenrirECS's public interface.
- [ ] Nothing in the repository refers to them.
- [ ] The old-interface `FenrirECSTests` cases are folded into the component-type tests with their expected values unchanged, or deleted where ticket 01's tests already pin the same behaviour. No behaviour they pinned loses coverage.
- [ ] ADR-0008's "What `FenrirECS` provides instead" list and its "Cost" section name `ComponentType` and its operations instead of `ComponentSelector`, `CountOf` and the `*Dynamic` methods. The decision and the "what this does and does not buy" section are otherwise unchanged.
- [ ] ADR-0008's "Cost" section says that, for a native type, any of the component-type operations (not only counting or selecting) may create its empty storage, which is what the code does since ticket 01.
- [ ] With the `*Dynamic` methods gone, the per-operation `IsDynamic()` branch in `EntityList` is written once (review of ticket 01: Repeated Switches), not seven times.
- [ ] No other doc (README, glossary, doc comments) still names the removed symbols.
- [ ] All tests pass (FenrirECS and scripting), `ScriptingDoesNotUseEnTT` passes, and the DemoApp builds.
