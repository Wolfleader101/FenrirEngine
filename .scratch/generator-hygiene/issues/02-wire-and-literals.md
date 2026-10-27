# 02: Iris.Bindings drops its unread wire, and the C# literals leave the model

**What to build:** `Natives.cs` builds nothing no writer reads, and `Binding.cs` holds the binding's model and no C# spelling (spec: "No wire", "Literals on the writers' side").

- **`packages/Iris/Tools/Iris.Bindings/src/Natives.cs`**: delete `WireParameter`, the `WireResult` and `Wire` members of `BoundNative` (and their doc), the `wire` list and `wireResult` locals in `Bind` with every `wire.Add(...)`, and the private `Wire(string kind)` helper. Then audit each remaining `BoundNative` member (`Function`, `Result`, `Parameters`, `Named`, `Outputs`, `Folded`, `Facade`, `FacadeParameters`, `NeedsSystem`) for a reader in `CSharpWriter.cs`, `CppWriter.cs`, `Generator.cs` or the tests; delete any with none and name them in the PR body.
- **`CSharpLiteral.cs`** (new, beside `CSharpWriter.cs`): `internal static class CSharpLiteral` with `Integer`, `Char`, `Float` moved from `Binding.cs` unchanged, and `Bool` and `Pointer` for the two inline cases of today's `Default` switch. Each throws the same `BindingException` text it throws today.
- **`CSharpWriter.cs`**: `Default`, `StructDefault` and `ElementDefault` move here as private methods taking the `Binding` (they look up a described type's fields and a bound type's constructor), calling `CSharpLiteral` for scalars. `HasZeroDefaults`/`IsZero` stay in `Binding.cs` if they do not spell C#; if they share code with the moved methods, say which way it went.
- **`Binding.cs`**: no `CultureInfo`, no literal spelling, no `"{where}: a <kind>'s default is"` message remains.

**Blocked by:** none

**Touches:** `packages/Iris/Tools/Iris.Bindings/src/Natives.cs`, `packages/Iris/Tools/Iris.Bindings/src/Binding.cs`, `packages/Iris/Tools/Iris.Bindings/src/CSharpWriter.cs`, `packages/Iris/Tools/Iris.Bindings/src/CSharpLiteral.cs`

**Status:** ready-for-agent

- [ ] `grep -n "Wire" packages/Iris/Tools/Iris.Bindings/src/*.cs` finds nothing; `grep -n "CultureInfo" packages/Iris/Tools/Iris.Bindings/src/Binding.cs` finds nothing.
- [ ] Iris.Bindings' tests pass unchanged; `fenrir-generate-bindings` is a no-op after the commit (no generated file changes).
- [ ] `check_tickets.py` passes.

## Comments

Filed from the generator-hygiene grilling (spec decisions 3, 5). Cut from `dotnet-scripting`; runs beside 01 and 03.
