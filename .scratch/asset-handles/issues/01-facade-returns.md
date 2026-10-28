# 01: `@facade Class.Method returns <out>`: a generated facade method that returns one output as its value

**What to build:** the generator rule the spec's "A facade mark may name the output the method returns" describes, in the two tools and their docs, with nothing in the engine changed.

- **`tools/describe-api/src/Natives.cs`**: the `@facade` line is `Class.Method` optionally followed by whitespace, the word `returns`, whitespace and a parameter name (`FacadeMark` grows, or a second regex splits the tail). The function record gains the named parameter (`FacadeReturns`, null when absent), written to the JSON as `"facadeReturns": "<name>"` beside `"facade"` (absent otherwise). Refusals, each naming the function and the mark as the existing ones do: `returns` names no parameter of the native; names one that is not a single `out` (`output` on a pointer followed by a count, an `inOut`, an `input`, or a plain parameter); the native has more than one single `out`; the native's result is neither `void` nor `bool`. `tests/Fixture/Natives.cpp` gains one native of the shape `bool F(..., T* out)` marked `returns`; `tests/Fixture/FailingNatives.cpp` gains the four refusals; `Fixture.natives.json` is regenerated; `DescribeTests.cs` pins the record and the refusals. `SCHEMA.md`'s `facade` row and doc-comment paragraph, and `packages/Iris/docs/natives-description.md`'s, say the form and the refusals.
- **`packages/Iris/Tools/Iris.Bindings/src/Natives.cs`**: `NativeFunction` reads `facadeReturns`; `FacadeName` (or `BoundNative`) carries the returned parameter; `Facade()` refuses, naming the mark, a `returns` whose parameter the binding cannot find among the bound parameters (describe-api already refused the shapes), and the public-shape checks apply to the remaining parameters. `FacadeParameters` leaves the returned parameter out; `FacadeArguments` passes it as `out <type> <name>`; the facade's result is the out's C# type.
- **`packages/Iris/Tools/Iris.Bindings/src/CSharpWriter.cs` `FacadeMethod`**: for a `returns` mark the method is a block body: the call with `out T name` (the `bool` result discarded with a leading `_ =` when the native returns one), then `return name;`. No conditional. `NativeDoc` drops the `<param>` of the returned parameter, writes its doc as the `<returns>` text, and ends it with "the C++ default when the call did nothing". Static or on `this` as today.
- **`packages/Iris/Tools/Iris.Bindings/README.md`**: the "Facade parts" paragraph and the `<generated>/<Class>.cs` row say the `returns` form, what the method returns on a failed call and why (ADR-0045's zeroing), and the refusal; `tests/Fixture/Native/Natives.cpp` (or wherever the fixture's natives live) gains the marked native, the committed generated facade shows the value-returning method, and `BindingTests.cs` gains: a `returns` mark generates a method returning the out's type with no out parameter; a bool-and-out native marked plain still generates `bool F(..., out T)`; a `returns` the binding cannot honour fails naming the mark. The stub-compile check covers the generated method.
- Regenerate: `fenrir-describe-api` and `fenrir-generate-bindings` after the tool change; every engine binding is a no-op (no engine native is marked `returns` yet).

**Blocked by:** none

**Touches:** `tools/describe-api/src/Natives.cs`, `tools/describe-api/tests/`, `tools/describe-api/SCHEMA.md`, `packages/Iris/docs/natives-description.md`, `packages/Iris/Tools/Iris.Bindings/src/Natives.cs`, `packages/Iris/Tools/Iris.Bindings/src/CSharpWriter.cs`, `packages/Iris/Tools/Iris.Bindings/README.md`, `packages/Iris/Tools/Iris.Bindings/tests/`

**Status:** ready-for-agent

- [ ] `dotnet test tools/describe-api/tests` and `dotnet test packages/Iris/Tools/Iris.Bindings/tests` pass with the new cases; `fenrir-describe-api` and `fenrir-generate-bindings` leave every engine binding unchanged.
- [ ] The fixture's committed `Fixture.natives.json` carries `facadeReturns` on one native, and its generated facade has a method returning the out's type and no out parameter.
- [ ] `check_tickets.py` and clang-format pass.

## Comments

Filed from the asset-handles grilling (spec decisions 3, 6, 7). Cut from `dotnet-scripting`; tickets 02 and 03 are each cut from this branch.
