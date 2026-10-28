Status: open

# Bound parameter: one typed record of a native parameter's crossing form inside Iris.Bindings

Origin: the third architecture review of `dotnet-scripting` (2026-10-28), candidate 6 (one typed parameter record inside Iris.Bindings), grilled on tip `73cce339` in two rounds, nine decisions, all recommendations accepted. The review's report is `architecture-review-20261028-dotnet-scripting-pass3.html` in the user's temp directory; its other candidates are specified elsewhere or not at all.

The vocabulary is the repo's: *native function*, *facade*, *binding*, *bound type*, *wrapper*, *crossing form* (new, this spec) from `GLOSSARY.md`; *module*, *interface*, *seam*, *locality*, *leverage*, *the deletion test* from the codebase-design skill.

## Problem statement

**The crossing form is decided once and re-derived ten times.** `Natives.Bind` in `packages/Iris/Tools/Iris.Bindings/src/Natives.cs` decides how each C++ parameter crosses to C# (a value, `in T`, `out T`, `T*`, `Span<T>`, `ReadOnlySpan<T>`, text) and records the decision as C# text, `in Vector3 origin` or `ReadOnlySpan<Hit> hits`, in `BoundNative.Parameters`. Everything that needs the form afterwards reads it back out of the text by prefix and substring. On the tip there are ten such sites, up from the six the review counted before the asset-handles spec added `returns` marks: six members of `BoundNative` (`FacadeResult`, `FacadeReturnedName`, `FacadeReturningArguments`, `NeedsSystem`, `FacadeArguments`, `FacadeParameterTypes`, through the helpers `TypeOf` and `ParameterName`) and four inside `Bind` and its helpers (the `onThis` test `parameters[0].StartsWith("in Mirror ")`, the `facadeParameters` strip of `"in "`, `FacadeReturned()`'s `StartsWith("out ")`, and `Facade()`'s `EndsWith('*')` on each parameter and on the result). `CSharpWriter.FacadeMethod` tests the result string once more (`== "void"`). Beside the text, `Bind` keeps three parallel lists that carry per-parameter facts the text cannot: `Outputs` (the C++ indices the registration's zeroing names), `Folded` (the count and length parameters a span absorbs, which `NativeDoc` skips) and `Named` (the described types, per native).

The facade writer's correctness therefore depends on the natives writer's spelling one method earlier: a parameter spelled `in  Vector3 origin` (two spaces) or a type with a space in it would pass `Bind` and break every reader. The last two changes to this code (the `[in]` mark, `returns`) each added a reader rather than a field, which is how six became ten.

**What is not the problem.** The SDK's Roslyn generator (`NativeFunctionGenerator.cs`) classifies the C# declaration again to write the wire call, and Iris owns the wire (`InternalCall.hpp`, `ValueKinds.cs`). Roslyn only sees C#, and the wire is Iris's, so neither can share this record; the candidate is Iris.Bindings alone. The describe-api side is considered and left as it is (decision 4).

## Solution

### One record per parameter, the form as a closed enum

`BoundNative.Parameters` becomes `IReadOnlyList<BoundParameter>`:

```text
BoundParameter(Crossing Form, string Type, string Name, int Index, string? Folded, NativeParameter Source)
Crossing: Value | In | Out | Pointer | Span | ReadOnlySpan | Text
```

- `Form` is the crossing form, the seven the README's "Natives" paragraph already lists. `Pointer` covers `T*` (an `inOut`) and `void*`; `Text` covers both `ReadOnlySpan<char>` (a `std::string_view`) and `string` (`const char*` plus a length), which the `Type` tells apart.
- `Type` is the C# type with no modifier and no span wrapping (`Hit`, `Vector3`, `char`, `void`), `Name` the C# identifier (keyword-escaped as today).
- `Index` is the parameter's position in the C++ function's full parameter list, the call context and state counted: what `ZeroingOutputs<&F, i, j>` names.
- `Folded` is the C++ name of the count or length a span or text absorbs, null otherwise.
- `Source` is the described `NativeParameter`, for its doc and its C++ name.

The record spells its own strings, once each: `Declaration` (`in Vector3 origin`, `ReadOnlySpan<Hit> hits`, `Hit* cursor`), `FacadeParameter` (the declaration with `In` dropped to a value, as the README's one facade rule says), `FacadeArgument` (`in origin`, `out hit`, `hits`). `BoundNative`'s members become reads of the record: `NeedsSystem` is "any `Span`, `ReadOnlySpan` or `Text` spelled as a span", `Outputs` is the indices of the `Out` forms, `Folded` the non-null folded names, `FacadeParameterTypes` the types, `FacadeReturned` a `BoundParameter?` rather than a string, and the `onThis` test compares a form and a type rather than a prefix. The three parallel lists are derived views, not state `Bind` keeps consistent.

### The result the same way

`BoundNative.Result` becomes `BoundResult(Crossing Form, string Type)` with the forms `None`, `Value` and `Pointer` (the same enum, three members used), spelled once. The facade's refusal ("cannot be public: it returns void*"), the `returns` check (the result is `None` or `bool`) and the value-returning body (`_ =` before a non-`None` call) read the form.

### What does not change

The decisions `Bind` makes, in the order it makes them, and every message it refuses with. The eight engine bindings and the Iris.Bindings fixture regenerate byte-for-byte. The README's "Natives" and "Facade parts" paragraphs describe outputs, which are unchanged, and are untouched; the record's XML doc lists the seven forms and cites the README paragraph as the rule it spells, so the rule is stated in prose once and in code once. `CppWriter.Registration` keeps reading `native.Outputs`. The SDK's generator, Iris's wire, describe-api and its schema are untouched.

### describe-api stays raw

The natives description keeps recording the raw type (kind, pointee, const) and the three doc marks (`output`, `inOut`, `input`), and Iris.Bindings stays the one interpreter. The crossing form is a binding fact, not a C++ fact: a pointee's C# name, whether a struct is a wrapper, and a `[natives] types` alias all come from the binding's configuration and descriptions, so describe-api could at best record "a span or not", the one structural rule (an `i32` follows, no `[in]` mark), three lines of `Bind`. A description can be hand-written (the fixture's is), so Iris.Bindings would have to check a recorded form against the raw type anyway, as it re-checks `returns` today, and a second interpreter would appear rather than one vanishing. And the schema change would regenerate nine committed `.natives.json` files for no change in meaning.

### Struct-by-value stays out of scope

ADR-0044 left a struct crossing by value with its size open. Adding it today would touch describe-api's `Natives.cs`, `SCHEMA.md`, `natives-description.md`, Iris.Bindings' `Natives.cs`, `NativeFunctionGenerator.cs`, `InternalCall.hpp`, `ValueKinds.cs` and `InternalCalls.cs`. After this spec the Iris.Bindings share of that is one enum member and one spelling; nothing here adds the form, since nothing passes a struct by value and the wire is Iris's decision.

### Tests

The refactor is proven by the outputs, not by the record. The 118 Iris.Bindings cases already pin every form's spelling: declarations (`FindAll(in Vector3 origin, Span<Hit> hits)`, `Sum(ReadOnlySpan<uint> values, ...)`, `Next(ulong* cursor, void* body)`, `Say(string text)`, `Show(ReadOnlySpan<char> text, ...)`), facade parts, methods on a mirror (static and on `this`), the value-returning method, the zeroing indices (`ZeroingOutputs<&Find, 4>`, `<&Extremes, 3, 4>`) and the folded docs. No fixture output and no unit test of `BoundParameter` are added: the record is `internal`, and the interface is the test surface. The ticket's checklist is that both suites pass unchanged and that every fixture file and all eight engine bindings regenerate with 0 changed.

## Decisions (from the grilling)

1. The candidate is accepted in the shape above: `Bind` decides once, a typed record carries it, the ten re-parses become field reads, outputs byte-for-byte unchanged.
2. The record is `BoundParameter(Form, Type, Name, Index, Folded, Source)` with `Crossing` a closed enum of the README's seven forms, spelling its own declaration, facade parameter and facade argument; `Outputs`, `Folded` and `NeedsSystem` are derived from it.
3. The result becomes `BoundResult(Form, Type)` with `None`, `Value`, `Pointer`.
4. describe-api stays raw; Iris.Bindings is the one interpreter. Recorded here, no ADR (no ADR said otherwise).
5. Struct-by-value stays out of scope; the spec names it only as what the record makes cheaper.
6. Names: `BoundParameter`, `Crossing`, `BoundResult`; `GLOSSARY.md` gains *crossing form*.
7. No new fixture output and no record-level test; the checklist is 0 changed everywhere.
8. One ticket. It names its overlap with the sibling 1+7 spec on `CppWriter.cs` (that one may edit the defaults-test writer; this one touches the registration's `Outputs` lines) and stacks on whichever lands first.
9. The README is untouched; the record's doc cites it.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `BoundParameter`, `Crossing` and `BoundResult` in Iris.Bindings' `Natives.cs`; the natives, facade and registration writers read them; 0 changed outputs | none |

Branch `bound-parameter-01-typed-record`, cut from `dotnet-scripting`, or from the 1+7 spec's ticket branch if that has landed on `CppWriter.cs` first.

## Done looks like

- `grep -c "StartsWith\|LastIndexOf\|IndexOf(' ')\|EndsWith" packages/Iris/Tools/Iris.Bindings/src/Natives.cs` is 0; `grep -c '== "void"' packages/Iris/Tools/Iris.Bindings/src/CSharpWriter.cs` is 0.
- `BoundNative.Parameters` is `IReadOnlyList<BoundParameter>`, `BoundNative.Result` is a `BoundResult`, and `FacadeReturned` is a `BoundParameter?`.
- `dotnet test packages/Iris/Tools/Iris.Bindings/tests` passes 118 and `dotnet test tools/describe-api/tests` passes 38, unchanged; `git status` is clean after regenerating the fixture outputs and running `dotnet run --project packages/Iris/Tools/Iris.Bindings/src -- <toml>` for each of the eight engine bindings (the README's command; no engine build is needed).
