Status: resolved

# Built-in layouts: the built-in extension's last hand-agreed layouts get one owner each

Origin: the third architecture review of `dotnet-scripting` (2026-10-28), candidate 5 (the built-in extension's last hand-agreed layouts), grilled to seven decisions on 2026-10-28, every recommendation accepted. Candidates 1+7, 4 and 6 are being grilled in sibling sessions; candidate 4 edits `src/Scripting.cpp`, which ticket 03 here touches in six lines (see "Out of scope").

The vocabulary is the repo's: *shared struct*, *API description*, *binding*, *event reader*, *query*, *script report*, *system host*, *native function* from `GLOSSARY.md`; *module*, *interface*, *seam*, *locality*, the deletion test from the codebase-design skill.

## Problem statement

Everything else that crosses between C++ and C# is a described struct on the generated pipeline (ADR-0042): one C++ definition, a generated mirror, and Iris's field-by-field check when a script assembly loads. Three things in the built-in extension are still literals agreed by hand on both sides, with prose as the only statement of the agreement. The deletion test says concentrate: delete the prose and both sides must still agree, so the agreement is real and is just spelled twice.

**(a) The event view.** `packages/FenrirScripting/src/Bindings.cpp` `EventReadAfter` takes a `Detail::NativeAddress* view` and writes five words into `view[0..4]`, copied one by one out of FenrirApp's `RawEventRange` (`packages/FenrirApp/include/FenrirApp/App.hpp:40`: `older`, `olderCount`, `newer`, `newerCount`, `missed`, all defaulted). `Managed/Fenrir.Scripting/Events.cs` `EventReader<T>.Read` reads them back from a `stackalloc nuint[5]` with a comment naming the order. The natives description records the parameter as a pointer to `u64` marked `inOut` (its doc tag says `[in,out]`; the native never reads it). No struct names the layout on either side.

**(b) The query row stride.** A query row is the entity, then one address per component. The entity's share is spelled as the number two in four places: `src/Queries.hpp:12` (`EntityWords = 2`; `Queries.cpp:64` already `static_assert`s it against `sizeof(Entity)`), `Managed/Fenrir.Scripting/QueryNative.cs:16` (`EntityWords = 2`), and `Managed/Fenrir.Scripting.Generators/QueryGenerator.cs`, which writes `arity + 2` as the stride (line 69) and `row[i + 1]` as the index of component `i` (lines 85 and 114; `i` is 1-based, so the `+ 1` is the two-word entity again). `QueryCount`'s doc says "two words" in prose. C# already holds the Iris-checked `Entity` mirror whose size is the fact these all restate.

**(c) The generated-type names.** `src/ScriptData.cpp:353-367` finds `Fenrir.Scripting.Generated.ComponentTable` and `EventTable` by name and their `Register` method by name, as `ComponentFieldsGenerator.cs:76-82` and `EventGenerator.cs:58-63` write them; `src/SystemHostNames.hpp` holds `Fenrir.Scripting.Generated.SystemHost` and its four method names, as `SystemGenerator.cs:80-96` writes them. The C++ side spells seven names in two places; the generators spell them inline in three files (with doc-comment copies in `EventGenerator.cs:13`, `SystemGenerator.cs:12` and `Systems.cs:110`); `Tests/BadReports/BadReports.cs` hand-writes the two tables on purpose, so a test can make one report something wrong.

**What the review got wrong, found while grilling.** The C++ side of (b) is already asserted (`Queries.cpp:64`). Drift in (c) is not "a failed load as the only check": every end-to-end scripting test loads a real generated assembly, so a renamed host or table fails the suite in CI. An `internal-structs` mirror cannot be registered with Iris (Iris's shared-struct check wants a public C# struct; the asset-handles spec hit this with `ScriptAssetHandle`), so an internal mirror is generated and size-asserted but not checked at load.

## Solution

### (a) The event view is FenrirApp's `RawEventRange`, described by FenrirApp

By ADR-0042 a type's package owns its description. FenrirApp gets `packages/FenrirApp/api/FenrirApp.api.toml` selecting `../include/FenrirApp/App.hpp` with `"Fenrir::RawEventRange" = "shared-struct"`, `fenrir_describe_api(FenrirApp api/FenrirApp.api.toml)` in its `CMakeLists.txt`, and the committed `FenrirApp.api.json` (two `pointer` fields and three `u64` fields, size 40, alignment 8; its `Count()` method is no obstacle, as `Entity`'s methods are not). The built-in binding (`packages/FenrirScripting/api/FenrirScripting.binding.toml`) lists `../../FenrirApp/api/FenrirApp.api.json` in `descriptions`, as it lists FenrirECS's, and the `iris_generate_binding(FenrirScripting ...)` call in `packages/FenrirScripting/CMakeLists.txt` names it too. `[cpp] includes` gains `FenrirApp/App.hpp` for the mappings header.

The mirror is `internal`, like `ScriptAssetHandle`: `internal-structs = ["Fenrir::RawEventRange"]` and `by-hand = ["Fenrir::RawEventRange"]`, named `RawEventRange` in `Fenrir.Scripting`, used only by `EventReader<T>.Read`. The agreement is the pipeline itself: one header, a generated mirror, CI regenerating and failing on a dirty tree, and the mappings header's size and alignment assert. It is not Iris-checked at load, and that is accepted: a public mirror would buy the load check and a generated defaults case at the cost of a public type scripts never use.

`EventReadAfter` becomes `bool EventReadAfter(const CallContext&, Detail::EngineContext&, std::int32_t event, std::uint64_t* seen, RawEventRange* view)` with `@param[out] view`, writing `*view = queue->ReadRawAfter(cursor)`. The generated declaration is `out RawEventRange view`; the registration zeroes it (ADR-0045), so the no-such-event path leaves it at its default, which C# never reads because it throws. `Read` becomes `Native.EventReadAfter(id, cursor, out RawEventRange view)` and builds the `EventRange<T>` from the fields. The `types = { "Fenrir::Detail::NativeAddress" = "nuint" }` line in `[natives]` stays if another native still uses the alias; the ticket checks and deletes it, and the alias in `EngineContext.hpp`, when nothing does.

### (b) Both sides derive the entity words from `Entity`

C++: `Queries.hpp` says `inline constexpr std::size_t EntityWords = sizeof(Entity) / sizeof(std::uintptr_t);` with a `static_assert` that the division is exact and `alignof(Entity) <= alignof(std::uintptr_t)` (the assert in `Queries.cpp` moves up and loses nothing). `QueryCount`'s doc in `Bindings.cpp` says "the entity, `sizeof(Entity)` in words" instead of "two words".

C#: `QueryNative.EntityWords` becomes `internal static readonly int EntityWords = sizeof(Entity) / sizeof(nuint);` (C# cannot make `sizeof` of a struct a `const`; the JIT folds a `static readonly int` after tier-up, and a query's cost is its two native calls). `QueryGenerator` writes `QueryNative.EntityWords` where it wrote `2` and `i + 1`: the stride is `arity + QueryNative.EntityWords` and component `i`'s address is `row[QueryNative.EntityWords + (i - 1)]`, with the arithmetic folded in the generated text where it is a constant (`QueryNative.EntityWords + 0` is written `QueryNative.EntityWords`). The `Entity` mirror is Iris-checked when a script assembly loads, so C# derives the words from a checked layout and C++ from the engine's own type; the number two is spelled nowhere.

### (c) The generated-type names are concentrated, not shared

**No shared owner.** A file both sides read would make the Roslyn generators read a shipped data file (an `AdditionalFiles` entry through `Fenrir.Scripting.props`) for seven strings the end-to-end tests already pin, and ADR-0042 already settled that the SDK's generators spell their own constants. Instead each side spells the seven names once:

- C++: `src/SystemHostNames.hpp` becomes `src/GeneratedNames.hpp`, namespace `Fenrir::Detail::GeneratedNames`, holding the system host's type and four method names (unchanged), the two table types (`ComponentTable`, `EventTable`) and `Register`. `ScriptData.cpp`'s `Report` reads the table and method names from it; `Scripting.cpp`'s include and five qualified names follow the rename. The header's comment names the three generator files and says the end-to-end tests (`SystemTests.cpp`, `AdmissionTests.cpp`, every test that loads a generated assembly) are what pins the names.
- Generators: a new `Managed/Fenrir.Scripting.Generators/GeneratedNames.cs`, an `internal static class GeneratedNames` with the namespace, the three type names, the three hint names and the five method names as `const string`s; `ComponentFieldsGenerator.cs`, `EventGenerator.cs` and `SystemGenerator.cs` build their text from it. The doc-comment mentions (`EventGenerator.cs:13`, `SystemGenerator.cs:12`, `Systems.cs:110`) stay as prose and say where the names live. `BadReports.cs` stays hand-written: it is a deliberate stand-in, and it says so.

**ADR-0042 is amended** with one sentence (applied in this spec's PR) beside its "the SDK's names live once in a FenrirScripting-owned defaults file" clause: the names of the types the SDK's generators write (`Fenrir.Scripting.Generated.SystemHost`, `ComponentTable`, `EventTable` and their methods) are the generators' own constants and `GeneratedNames.hpp`'s, pinned by the end-to-end tests, and are not in the defaults file, which Iris.Bindings reads and the generators do not.

### Out of scope

- Any change to what the three natives do, to `EventRange<T>`, to the query types' public shape, or to the registry.
- A public `RawEventRange` mirror and its Iris load check (decision 3).
- `Scripting.cpp` beyond the include line and five qualified names at (today) lines 9 and 177 to 190: candidate 4's spec edits `Install` in the same file; whichever lands second rebases.
- A generators test project: none exists, and the end-to-end tests are the pin.

## Decisions (from the grilling)

1. One spec, three tickets with disjoint Touches, no Blocked by lines, each cut from `dotnet-scripting` and run side by side.
2. The event view's one definition is FenrirApp's `RawEventRange`, described by a new FenrirApp API description that the built-in binding lists; no scripting-side copy struct.
3. The mirror is `internal-structs` + `by-hand`, as `ScriptAssetHandle` is: generated and size-asserted, not Iris-checked at load.
4. C++ and C# each derive the entity's words from `sizeof(Entity)`; the generator writes `QueryNative.EntityWords`, never `2` or `i + 1`.
5. The generated-type names get one C++ header (`GeneratedNames.hpp`) and one generator constants class; no file both sides read; no new test.
6. ADR-0042 gains one amendment sentence recording decision 5, so a later review does not re-suggest a shared owner.
7. Ticket 03's rename touches `Scripting.cpp`'s include and five names, named in its Touches as an overlap with candidate 4.

No glossary change: no new concept is named (the header and the constants class are files, not terms).

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | FenrirApp's API description, `RawEventRange` as the view: `Bindings.cpp`, `Events.cs`, the built-in binding's configuration and generated outputs | none |
| 02 | Entity words from `sizeof(Entity)` on both sides: `Queries.hpp/.cpp`, `QueryNative.cs`, `QueryGenerator.cs`, `QueryCount`'s doc | none |
| 03 | `GeneratedNames.hpp` and `GeneratedNames.cs`: `ScriptData.cpp`, `Scripting.cpp`'s six lines, the three generators | none |

Branches `builtin-layouts-01-event-view`, `-02-entity-words`, `-03-generated-names`, each cut from `dotnet-scripting`, each PR targeting it. Tickets 01 and 02 both edit `Bindings.cpp` (01 the `EventReadAfter` native, 02 the `QueryCount` doc comment, 25 lines apart): disjoint hunks, so they run side by side and the second to land rebases.

## Done looks like

- `grep -rn "nuint\[5\]\|view\[" packages/FenrirScripting/Managed/Fenrir.Scripting/Events.cs packages/FenrirScripting/src/Bindings.cpp` finds nothing; `packages/FenrirApp/api/FenrirApp.api.json` exists and describes `Fenrir::RawEventRange`; `grep -n "RawEventRange" packages/FenrirScripting/Managed/Fenrir.Scripting/Generated/*.cs` finds the mirror and the `out RawEventRange` declaration.
- `grep -rn "EntityWords = 2\|arity + 2\|i + 1\]" packages/FenrirScripting/src packages/FenrirScripting/Managed` finds nothing; `grep -rn "two words" packages/FenrirScripting/src/Bindings.cpp packages/FenrirScripting/Managed` finds nothing.
- `packages/FenrirScripting/src/SystemHostNames.hpp` does not exist; `grep -rn "Fenrir.Scripting.Generated" packages/FenrirScripting/src packages/FenrirScripting/Managed/Fenrir.Scripting.Generators --include=*.cpp --include=*.hpp --include=*.cs` finds string literals only in `GeneratedNames.hpp` and `GeneratedNames.cs` (doc comments and `BadReports.cs` excepted).
- `fenrir-describe-api` and `fenrir-generate-bindings` re-run with no further change after each ticket; every scripting test passes under the msvc-x64-debug preset; the other presets are CI's.
