Status: ready-for-agent

# Generator hygiene: the two tools describe one thing one way and carry nothing unread

Origin: the second architecture review of `dotnet-scripting` (2026-10-27), candidate 3 (one kind classifier in describe-api; Iris.Bindings drops its unread wire) rolled up with two of candidate 7's small deletions (a pin on Iris.Bindings' fixture copy; the C# literal formatting out of the model), grilled to six decisions the same day.

The vocabulary is the repo's: *API description*, *natives description*, *binding*, *native function* from `GLOSSARY.md`; *module*, *interface*, *implementation*, *locality* from the codebase-design skill.

## Problem statement

**Two classifiers, one disagreement.** `tools/describe-api/src/Describer.cs` classifies a field's scalar type in one switch and `tools/describe-api/src/Natives.cs` classifies a native's parameter and result types in another. They agree on every kind but `char`: the field classifier says `u8` ("a byte of text, unsigned on every target, so a description reads the same everywhere"), the natives classifier sends `char` through `IntegerKind`, which follows the host compiler's signedness. `packages/Iris/docs/natives-description.md` documents the disagreement as a rule. The committed `packages/FenrirScripting/api/FenrirScripting.natives.json` carries eighteen `i8` entries (the `const char*` text parameters of the built-in natives) that a target with unsigned `char` would regenerate as `u8`, and CI's dirty-tree check would then fail on a correct tree. Nothing downstream reads the kind: Iris.Bindings recognises text by the type name `char`.

**An unread wire.** Iris.Bindings' `Natives.cs` builds, for every native, a `WireResult` string and a `Wire` list of `WireParameter(Type, Output)` records: the C# types of the bound function pointer's result and parameters. No writer and no test reads them; the SDK's Roslyn generator derives the function pointer from the C# declaration itself. Sixteen lines build what nothing consumes.

**A fixture nobody regenerates.** describe-api's test fixture (`tools/describe-api/tests/Fixture/`) is described by the `fenrir-describe-api` target through an interface target and pinned byte for byte by its own test. Iris.Bindings' fixture (`packages/Iris/Tools/Iris.Bindings/tests/Fixture/`) is a copy of the same header, differing only in its comments, with a hand-maintained `Fixture.api.json` that has drifted: it lacks `Fixture::Pair<double>`, which the header declares and describe-api's description records. Nothing regenerates it and nothing notices.

**C# spelling in the model.** `packages/Iris/Tools/Iris.Bindings/src/Binding.cs` (932 lines) is the binding's model: the described types, their C# names, the used bindings, the bound types. It also holds `Default`, `StructDefault`, `ElementDefault`, `Integer`, `Char` and `Float`, which spell a described default as a C# literal (`0.05f`, `float.PositiveInfinity`, `'é'`, `-0d`). `CSharpWriter.cs` is their only caller.

## Solution

### One scalar classifier

`Describer.ScalarKind(CXType)` returns the kind of a scalar type (`bool`, `char16`, `u32` for `char32_t`, `f32`, `f64`, `u8` for `char` of either signedness, and `IntegerKind`'s answer for the integers) or null. The field classifier's switch and the natives classifier's switch both call it first and keep only what is their own: the field classifier its handle, enum, record, pointer and array cases; the natives classifier its aliases (`alias ?? FixedWidth(kind)`), `std::string_view`, records, enums, pointers and the reference error. `char` is `u8` everywhere, with the field classifier's comment as the one statement of the reason. `natives-description.md`'s `kind` row says `char` is `u8`. `FenrirScripting.natives.json` is regenerated (eighteen entries); no other committed description changes, since no other binding's natives take `char`.

### No wire

`WireParameter`, `BoundNative.WireResult`, `BoundNative.Wire`, the `wire` list and `wireResult` locals in `Natives.Bind`, and the private `Wire(kind)` helper are deleted. The ticket audits the remaining `BoundNative` members (`Result`, `Parameters`, `Named`, `Outputs`, `Folded`, `Facade`, `FacadeParameters`, `NeedsSystem`) for a reader in `CSharpWriter.cs`, `CppWriter.cs` or the tests, and deletes any that has none, naming them in the PR. The compiler is the test.

### The fixture pinned

Iris.Bindings keeps its own `Fixture.hpp`: its comment says why it is there (the generated mappings and defaults test compile against it). It gains `tests/Fixture/Fixture.api.toml` selecting the header as describe-api's own `Fixture.api.toml` does, and `tools/describe-api/CMakeLists.txt`, which already describes its own fixture, adds an `IrisBindingsFixtureHeader` interface target over Iris's fixture directory and `fenrir_describe_api(IrisBindingsFixtureHeader ...)` on it. `fenrir-describe-api` then regenerates `Fixture.api.json` (returning `Fixture::Pair<double>`), `fenrir-generate-bindings` regenerates the outputs under `tests/Fixture/` from it, and CI's dirty-tree check pins both. Iris's own CMake stays free of Fenrir's module. `Fixture.natives.json` stays hand-maintained: Iris's fixture has no `Natives.cpp`, and a JSON-reading tool's fixture may be JSON.

### Literals on the writers' side

`CSharpLiteral.cs`, a static class beside `CSharpWriter.cs`, takes the pure scalar spellings: `Integer`, `Char`, `Float`, and the bool and pointer cases of the dispatch. `Default`, `StructDefault` and `ElementDefault`, which need the model (a described type's fields, a bound type's constructor), move to `CSharpWriter` as private methods taking the binding. `Binding.cs` keeps the model. No behaviour changes; the defaults tests and the fixture's generated mirrors are the test.

### Out of scope

- Any change to what the descriptions record beyond `char`'s kind.
- The built-in natives themselves (the built-in-extension spec).
- A glossary term or an ADR: the `u8` rule is ADR-0042's "reads the same everywhere", already stated in the Describer.

## Decisions (from the grilling)

1. One spec, three tickets touching disjoint directories, run side by side off `dotnet-scripting`.
2. One `Describer.ScalarKind` both classifiers call; `char` is `u8`; one regenerated description.
3. Delete the wire; audit the rest of `BoundNative`; no new test.
4. Iris's fixture gets a selection file and is described from describe-api's CMakeLists; the natives JSON stays hand-maintained.
5. Scalar literal spelling moves to `CSharpLiteral.cs`; the dispatch that needs the model moves to `CSharpWriter`.
6. No ADR, no glossary change; `natives-description.md`'s `kind` row changes.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `Describer.ScalarKind`; both classifiers on it; `natives-description.md`; `FenrirScripting.natives.json` regenerated | none |
| 02 | The wire deleted and `BoundNative` audited; `CSharpLiteral.cs`; `Default` and friends into `CSharpWriter` | none |
| 03 | `Fixture.api.toml` in Iris's fixture; describe-api's CMakeLists describes it; fixture description and outputs regenerated | none |

No two tickets touch a file in common (01 is `tools/describe-api/src/`, `packages/Iris/docs/` and one description; 02 is `Iris.Bindings/src/`; 03 is `Iris.Bindings/tests/Fixture/` and `tools/describe-api/CMakeLists.txt`), so each is cut from `dotnet-scripting` and targets it. Branches `generator-hygiene-01-scalar-kind`, `-02-wire-and-literals`, `-03-fixture-pin`.

## Done looks like

- `grep -n "CXType_Char_S" tools/describe-api/src/*.cs` finds one case, in `ScalarKind`; `grep -rn '"i8"' packages/FenrirScripting/api/FenrirScripting.natives.json` finds nothing.
- `grep -n "Wire" packages/Iris/Tools/Iris.Bindings/src/Natives.cs` finds nothing.
- `grep -n "CultureInfo\|BindingException(\$\"{where}: a " packages/Iris/Tools/Iris.Bindings/src/Binding.cs` finds nothing; `Binding.cs` is under 800 lines.
- `fenrir-describe-api` regenerates Iris's `Fixture.api.json` and is a no-op after the commit; `Fixture::Pair<double>` is in it.
- describe-api's and Iris.Bindings' tests pass; both generators are no-ops after each ticket's commit; the msvc-x64-debug preset builds.
