Status: ready-for-agent

# Test FenrirECS's dynamic components directly, with Catch2

Origin: architecture review of FenrirScripting, candidate 4. ADR-0008 names this as the right home for these tests.

## Problem Statement

`FenrirECS` is the module that owns the engine's ECS library. Its dynamic components (storage for a component type known only at run time) and its mixed native-and-dynamic queries are only tested today by driving them from the scripting package's tests, which need the .NET runtime, a built fixture assembly and the scripting option turned on. A bug in the ECS has to be reproduced through C#, and a developer who builds the engine without scripting has no tests for the ECS at all.

## Solution

From a maintainer's point of view, `FenrirECS` has its own test executable that builds and runs without .NET or scripting. It exercises the ECS through `EntityList`'s public interface, using Catch2 as the test framework. The same behaviour continues to be exercised through scripting too; the new tests add direct coverage and make failures localised.

## User Stories

1. As a maintainer, I want FenrirECS tests that run without .NET, so that I can check the ECS on any machine that can build the engine.
2. As a maintainer, I want the tests to run from CTest, so that one command runs everything.
3. As a maintainer, I want a failing ECS test to name the ECS behaviour, so that I do not debug through C#.
4. As a maintainer, I want to use Catch2, so that writing a test is a few lines with readable failure output.
5. As a maintainer, I want the test framework pinned to a fixed release, so that builds are reproducible.
6. As a maintainer, I want the framework dependency to follow how the repo already brings in libraries, so that there is one convention.
7. As a maintainer, I want building the engine without tests to need no test framework, so that the shipping build is unchanged.
8. As a script-package maintainer, I want dynamic component behaviour covered at the ECS, so that scripting tests can stay about scripting.
9. As a maintainer, I want to test that a dynamic component is zeroed when added, so that a new script component never holds garbage.
10. As a maintainer, I want to test that a dynamic component is removed with its entity, so that no stale data outlives it.
11. As a maintainer, I want to test that `AddDynamic` refuses a dead entity and that `GetDynamic`, `HasDynamic` and `RemoveDynamic` on an absent component do not create storage.
12. As a maintainer, I want to test that elements honour the declared alignment and never move when other entities add or remove components.
13. As a maintainer, I want to test that `CountDynamic` and `CountOf` agree with what was added.
14. As a maintainer, I want to test that `ClearDynamic` removes every component of a type in the list and reports how many.
15. As a maintainer, I want to test that two dynamic types with different names have separate storage.
16. As a maintainer, I want to test that a dynamic type whose storage id would collide with another dynamic name, or with a native storage, is refused instead of corrupting memory (the defence ADR-0008 describes).
17. As a maintainer, I want to test that the ECS refuses to change a dynamic type's shape (size or alignment) while its storage is in use.
18. As a maintainer, I want to test that an invalid `DynamicComponentType` (zero size, non power-of-two alignment) reports itself invalid.
19. As a maintainer, I want to test `ForEachWith` visiting exactly the entities that have every selected component, for native-only, dynamic-only and mixed selectors.
20. As a maintainer, I want to test `ForEachWith` with an empty selector list, a selector with no matching entities, and a selector naming a dynamic type with no storage.
21. As a maintainer, I want to test that selecting a native component creates its (empty) storage but selecting a dynamic one does not (ADR-0008's stated cost).
22. As a maintainer, I want to test that structural changes made through the public interface while no iteration is running are visible to the next query.
23. As a maintainer, I want the tests to use only the public headers, so that swapping the ECS library underneath (ADR-0008) does not break them.
24. As a maintainer, I want no test to name EnTT, so that the tests keep proving the library is an implementation detail.
25. As a developer on any platform the engine supports, I want the test target to build under the same compiler presets as the rest, so that configurations do not diverge.

## Implementation Decisions

- Add a test executable for `FenrirECS`, linked to `FenrirECS` only (no scripting, no app, no .NET), registered with CTest.
- Use Catch2 v3 as the framework, at a pinned release. Bring it in the way the repo already brings in libraries (a git submodule under the package that uses it), not by downloading at configure time.
- Gate the test target behind a build option; with it off, nothing about Catch2 is configured, fetched or built. `enable_testing()` is called once at the top level, not only when scripting is on, so both packages' tests share one CTest run.
- The tests live beside `FenrirECS` and include only its public headers. They do not include the ECS library's headers directly. (The public headers themselves still include them; that is the known limit ADR-0008 records.)
- The scripting package's own harness stays as it is. Migrating it to Catch2 is not part of this work.
- No new glossary term is needed; the tests use *dynamic component*, *selector* and the other terms ADR-0008 already defines.

## Testing Decisions

- A good test here uses `EntityList` the way another package would: create entities, add and read components, query, and check observable results (counts, which entities were visited, returned pointers' alignment and stability, returned booleans). It never inspects the byte storage class or pool internals.
- The one seam is `EntityList`'s public interface (with `DynamicComponentType` and `ComponentSelector`), the same one scripting uses. The new tests are the first to cross it without .NET.
- Cover one scenario per behaviour in the user stories above. Cases that need many entities (alignment, pointer stability) use a few hundred, not thousands, to stay fast.
- Prior art: the scripting tests "the ECS holds dynamic components and refuses to change their shape while in use", "script component storage honours alignment and never moves elements", and the runtime-view case. These are the behaviours to reproduce directly; keep the scripting versions until they are shown to be redundant.

## Out of Scope

- Replacing or reorganising the scripting package's test harness.
- Testing native-component behaviour beyond what mixed queries need.
- Removing EnTT from the public headers (ADR-0008 calls that a larger, separate decision).
- Performance benchmarks.
- Adding Catch2 to other packages.

## Further Notes

- Catch2 was the user's choice of framework over reusing the scripting harness.
- Confirm the default presets still configure on a machine without the Catch2 submodule checked out; the option should keep them from touching it.
