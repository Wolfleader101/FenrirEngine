# 01: Iris.Bindings: an SDK key is required only when something needs it

**What to build:** Every SDK-naming key in a binding configuration becomes optional-until-needed, the way the three resource keys already are (spec: "Keys required only when something needs them").

- **`BindingConfig.cs`**: `CSharpConfig`'s `ComponentAttribute`, `ComponentInterface`, `ReadOnlyComponentInterface`, `EventAttribute`, `EventInterface`, `TaggedUnionAttribute` and `RegistrationConfig`'s `Extension`, `MakeComponent`, `MakeReadOnlyComponent`, `MakeEvent`, `MakeSharedStruct` are read with `OptionalString` and held nullable, as the resource ones are. `[natives]` `attribute`, `make` and `zero-outputs` stay required while `[natives]` is present (nothing in a natives binding can do without them); they become inheritable in ticket 02.
- **`Binding.cs`**: a check beside `CheckResources` demands each key when a mirrored or listed type needs it: the component attribute and interface for a mirrored component, the read-only component interface for one in `read-only`, the event attribute and interface for a mirrored event, the union attribute for a mirrored tagged union, `extension` whenever `[registration]` is present, and each `make-*` factory when the generated registration lists a type of that kind (a `by-hand` type needs none). The diagnostic reads like the resource one: `<path>: <Type> is a component, but [csharp] has no component-attribute`.
- **The writers** (`CSharpWriter.cs`, `CppWriter.cs`) reach the names through accessors on `Binding` that throw that diagnostic, so no writer tests for null and the check cannot be bypassed by a writer the check forgot.
- **Configurations**: `packages/FenrirScripting/api/FenrirMath.binding.toml` and `packages/FenrirAssets/Scripting/api/FenrirAssets.binding.toml` lose the six names and the "None of these is used" comment; `tests/Fixture/Uses/Common.binding.toml` loses them too. Every other file is unchanged, and `fenrir-generate-bindings` writes nothing.
- **README** (`packages/Iris/Tools/Iris.Bindings/README.md`): the configuration listing marks each of these keys as needed only with a component, event, tagged union or the registration listing one, and the diagnostics paragraph gains the new message.
- **Tests** (`tests/BindingTests.cs`): the fixture with a component and no `component-attribute` fails naming the type and key; with no `make-event` while an event is listed, likewise; the Common binding (shared struct only) generates with none of the six.

**Blocked by:** none

**Touches:** `packages/Iris/Tools/Iris.Bindings/`, `packages/FenrirScripting/api/FenrirMath.binding.toml`, `packages/FenrirAssets/Scripting/api/FenrirAssets.binding.toml`

**Status:** resolved

- [x] `dotnet test` for Iris.Bindings passes with the new cases.
- [x] `fenrir-generate-bindings` is a no-op after the commit (CI's api-descriptions check).
- [x] No `*.binding.toml` carries a "None of these is used" comment.

## Comments

Filed from the binding-defaults grilling (spec decision 5). First of the stack: tickets 02 and 03 cut their branches from this one's.
