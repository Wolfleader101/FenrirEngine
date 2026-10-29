# 02: The query row's entity words come from `sizeof(Entity)` on both sides

**What to build:** the number two leaves the query stride; C++ and C# each derive the words a row spends on the entity from their `Entity` (spec: "(b) Both sides derive the entity words from `Entity`").

- **`packages/FenrirScripting/src/Queries.hpp`**: `inline constexpr std::size_t EntityWords = sizeof(Entity) / sizeof(std::uintptr_t);` (the header reaches `Entity` through `ComponentRegistry.hpp`'s include of `FenrirApp/App.hpp`; include `FenrirECS/Entity.hpp` directly if it does not), followed by `static_assert(sizeof(Entity) == EntityWords * sizeof(std::uintptr_t) && alignof(Entity) <= alignof(std::uintptr_t), "a query row holds the entity as the engine lays it out: value, then list");`. The comment above it says the words are the entity's size, not a number. **`Queries.cpp`**: the `static_assert` at (today) line 64 goes; nothing else changes. `Tests/Rules/QueriesTests.cpp` reads `EntityWords` and compiles unchanged.
- **`packages/FenrirScripting/src/Bindings.cpp`** `QueryCount`'s `@brief`: "a row is the entity (two words: a Fenrir::Entity, with its scene's list set)" becomes "a row is the entity (a Fenrir::Entity, `sizeof(Entity)` in words, with its scene's list set)". Rerun `fenrir-describe-api` and `fenrir-generate-bindings` so `api/FenrirScripting.natives.json` and `Generated/Native.cs` carry the new doc.
- **`packages/FenrirScripting/Managed/Fenrir.Scripting/QueryNative.cs`**: `internal static readonly int EntityWords = sizeof(Entity) / sizeof(nuint);` (a `const` cannot hold `sizeof` of a struct; the class is already `unsafe`), its comment saying it is the mirror's size, which Iris checks against C++ when a script assembly loads. The class comment's "two words" goes the same way. `Fill`'s `int stride = count + EntityWords;` compiles unchanged.
- **`packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/QueryGenerator.cs`** `AppendArity`: `stride` becomes the text `arity + QueryNative.EntityWords` (write it as `"(" + arity + " + global::Fenrir.Scripting.QueryNative.EntityWords)"` or the equivalent, parenthesised where it multiplies `index`); component `i`'s index `row[{i + 1}]` (two places) becomes `row[global::Fenrir.Scripting.QueryNative.EntityWords + {i - 1}]`, writing just `global::Fenrir.Scripting.QueryNative.EntityWords` when `i - 1` is zero. The comment "A row is the entity (two words: ...)" says the entity's words are `QueryNative.EntityWords`. `QueryNative` is `internal` in the contract and the generated code compiles into the script assembly: check that `QueryNative` is reachable there (the fixture assemblies already call `QueryNative.Fill` and `Release` from generated code through `InternalsVisibleTo` or because the generated types are internal; keep whatever makes that work today and say which in the PR).
- The generated query types' behaviour does not change: `Tests/QueryTests.cpp` and `Tests/Fixture/Queries.cs` (or wherever the fixture's query cases live) stay as they are.

**Blocked by:** none

**Touches:** `packages/FenrirScripting/src/Queries.hpp`, `packages/FenrirScripting/src/Queries.cpp`, `packages/FenrirScripting/src/Bindings.cpp` (the `QueryCount` doc comment only; ticket 01 edits `EventReadAfter` in the same file), `packages/FenrirScripting/api/FenrirScripting.natives.json`, `packages/FenrirScripting/Managed/Fenrir.Scripting/QueryNative.cs`, `packages/FenrirScripting/Managed/Fenrir.Scripting/Generated/Native.cs`, `packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/QueryGenerator.cs`

**Status:** claimed

- [x] `grep -rn "EntityWords = 2\|arity + 2\|i + 1\]\|two words" packages/FenrirScripting/src packages/FenrirScripting/Managed` finds nothing.
- [x] `fenrir-describe-api` and `fenrir-generate-bindings` re-run with no further change.
- [x] The scripting tests pass under the msvc-x64-debug preset with `Tests/QueryTests.cpp` and `Tests/Rules/QueriesTests.cpp` unchanged ("the widest query has eight components" exercises the stride at arity 8).
- [x] `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-28 in the spec worktree, stacked on the spec branch. The generated query types compile into Fenrir.Scripting itself (the generator emits only there), so `QueryNative.EntityWords` is reachable unqualified; no InternalsVisibleTo was needed. msvc-x64-debug: 17/17 scripting tests, query tests unchanged.


Filed from the builtin-layouts grilling (spec decision 4). Cut from `dotnet-scripting`; runs side by side with 01 and 03.
