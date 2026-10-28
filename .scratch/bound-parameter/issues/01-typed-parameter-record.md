# 01: `BoundParameter`: Iris.Bindings carries each native parameter's crossing form as a typed record, spelled once

**What to build:** the spec's "One record per parameter" and "The result the same way", inside Iris.Bindings only, with every generated output byte-for-byte unchanged.

- **`packages/Iris/Tools/Iris.Bindings/src/Natives.cs`**: add `internal enum Crossing { Value, In, Out, Pointer, Span, ReadOnlySpan, Text }` and `internal sealed record BoundParameter(Crossing Form, string Type, string Name, int Index, string? Folded, NativeParameter Source)` with `Declaration`, `FacadeParameter` and `FacadeArgument` properties that spell the strings `Bind` builds today (`in Vector3 origin` / `Vector3 origin` / `in origin`; `out Hit hit` / `out Hit hit` / `out hit`; `Span<Hit> hits` / same / `hits`; `ReadOnlySpan<char> text` and `string text` for `Text` by `Type`; `ulong* cursor`, `void* body` for `Pointer`). Add `internal sealed record BoundResult(Crossing Form, string Type)` (`None`, `Value`, `Pointer`) with its spelling. `BoundNative` takes `IReadOnlyList<BoundParameter> Parameters`, `BoundResult Result` and `BoundParameter? FacadeReturned`; `Outputs`, `Folded`, `NeedsSystem`, `FacadeResult`, `FacadeReturnedName`, `FacadeArguments`, `FacadeReturningArguments`, `FacadeParameters` and `FacadeParameterTypes` become reads of the records (`Outputs` is the `Index` of each `Out`; `Folded` the non-null `Folded`s; `NeedsSystem` any `Span`, `ReadOnlySpan`, or `Text` whose type spells a span). Delete `TypeOf` and `ParameterName`. In `Bind`, each branch that did `parameters.Add($"...")` constructs the record with the form, the C# type, the escaped name, `leading + index` and the folded name; `onThis` compares `Form == Crossing.In && Type == CSharpName(mirror)`; `facadeParameters` filters by reference and reads `FacadeParameter`; `FacadeReturned()` finds the `Out` whose `Name` matches; `Facade()` refuses on `Form == Crossing.Pointer` for a parameter and on `Result.Form == Crossing.Pointer`. The record's XML doc lists the seven forms and cites the README's "Natives" paragraph as the rule it spells.
- **`packages/Iris/Tools/Iris.Bindings/src/CSharpWriter.cs`**: `Natives` passes `native.Parameters.Select(p => p.Declaration)` and `native.Result.Declaration` (or equivalent) to `Signature`; `FacadeMethod` tests `native.Result.Form == Crossing.None` instead of `== "void"`; `NativeDoc` is unchanged in meaning (`Folded` stays a list of names).
- **`packages/Iris/Tools/Iris.Bindings/src/CppWriter.cs`**: `Registration` keeps reading `native.Outputs`; only the type of what it reads may change. The defaults-test writer in the same file is not touched: the sibling spec from review candidates 1+7 may edit it, so if that ticket lands first this branch is cut from it, and if this one lands first that one stacks on this.
- Nothing in `tests/`: no new case and no new fixture output. The first two fixture tests compare the committed outputs (`tests/Fixture` and `tests/Fixture/Uses`) with what the generator produces, so they are the fixture's 0-changed check; regenerate each of the eight engine bindings by the README's command (`dotnet run --project packages/Iris/Tools/Iris.Bindings/src -- <toml>`) and expect `git status` clean.
- Nothing in the README, `GLOSSARY.md` (the spec PR carries *crossing form*), describe-api, the SDK generator or Iris.

**Blocked by:** none

**Touches:** `packages/Iris/Tools/Iris.Bindings/src/Natives.cs`, `packages/Iris/Tools/Iris.Bindings/src/CSharpWriter.cs`, `packages/Iris/Tools/Iris.Bindings/src/CppWriter.cs`

**Status:** ready-for-agent

- [ ] `grep -c "StartsWith\|LastIndexOf\|IndexOf(' ')\|EndsWith" packages/Iris/Tools/Iris.Bindings/src/Natives.cs` is 0 and `grep -c '== "void"' packages/Iris/Tools/Iris.Bindings/src/CSharpWriter.cs` is 0.
- [ ] `dotnet test packages/Iris/Tools/Iris.Bindings/tests` passes 118 and `dotnet test tools/describe-api/tests` passes 38, both unchanged.
- [ ] Every fixture output and all eight engine bindings regenerate with 0 changed (`git status` clean).
- [ ] `check_tickets.py` and clang-format pass.

## Comments

Filed from the bound-parameter grilling (spec decisions 1 to 3, 7 to 9). Branch `bound-parameter-01-typed-record`, cut from `dotnet-scripting` unless the 1+7 spec's ticket has landed on `CppWriter.cs` first, in which case it is cut from that branch.
