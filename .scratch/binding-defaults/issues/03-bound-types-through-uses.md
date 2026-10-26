# 03: Iris.Bindings: bound types and their usings come through `uses` and the defaults file

**What to build:** A binding inherits `[bound-types]` from the bindings it uses and from its defaults file, a bound-type entry may name its mapping header, and the `using` an inherited bound type needs is derived as a used mirror's is (spec: "Bound types through `uses`", decisions 3, 4).

- **`BindingConfig.cs`**: `BoundType` gains `Include` (`include = "<header>"`, optional, valid on any entry). The defaults file's `[bound-types]` is read with the same shape; a `[bound-types]` entry the binding restates identically (type, constructor, default, wrapper, include) is refused by ticket 02's rule.
- **`Binding.cs`**: the bound types in force are the own entries, over the defaults file's, over every used binding's (transitively: a used binding's own in-force set). An inherited entry is consulted only for a described type here; the "names X, which no description describes" check stays on own entries. Two inherited sources (two used bindings, or a used binding and the defaults file) giving one name different entries fail naming both files; identical entries are one. `CSharpName`, `Default`, `IsZero` and the natives' wrapper lookup read the in-force set. `UsedNamespaces` also yields the namespace of the used binding whose `[bound-types]` supplies a type a mirror holds or a native takes (a defaults-file entry has no owner and yields none), so mirrors and natives naming `Vector3` get `using Fenrir.Math;` as they get `using Fenrir.Input;` for a used `WindowId` today.
- **`CppWriter.cs`**: after the binding's `[cpp] includes` and before the used bindings' mappings headers, one `#include` per distinct `include` of a bound type this binding binds (its own, inherited, in ordinal order).
- **Defaults file**: `packages/FenrirScripting/api/FenrirScripting.binding-defaults.toml` gains `[bound-types] "Fenrir::EntityValue" = { type = "Entity", constructor = ["value"], wrapper = "u32", include = "FenrirScripting/EntityValueMapping.hpp" }`; `tests/Fixture/Host.binding-defaults.toml` gains the fixture's `Ticket` the same way, with an include the fixture's mappings header already carries so the committed output does not change.
- **Bindings trimmed**: Physics, Renderer and DebugDraw lose `Fenrir::EntityValue` and the `FenrirScripting/EntityValueMapping.hpp` include line; the six bindings that use FenrirMath's lose their glm entries (`Vec2`, `Vec3`, `Vec4`, `Quat`, `Mat4`); Renderer loses the three `AssetHandle` entries it inherits from FenrirAssets's; every file loses `"Fenrir.Math"` from `usings`. Physics keeps `Fenrir::Transform` and `Fenrir::Entity`; Assets keeps its five handles; FenrirMath keeps its five glm types. Every generated file is byte for byte what it was, the `using` lines included.
- **README**: "Other bindings' types" says bound types are inherited too, with override and the identical-restatement refusal; the `[bound-types]` listing shows `include`; the diagnostics paragraph gains the two-sources message. ADR-0044's `[uses]` decision gains one sentence: a used binding's bound types are inherited as its mirrors are.
- **Tests** (`tests/BindingTests.cs`): a type bound only by the Common binding's `[bound-types]` is named here with Common's namespace in its `using`s; an own entry overrides an inherited one; two used bindings disagreeing fail naming both; an entry with `include` writes the header only when the type is bound here; the fixture's committed outputs are unchanged after the Fixture binding's trimmed entries.

**Blocked by:** 02

**Touches:** `packages/Iris/Tools/Iris.Bindings/`, `packages/FenrirScripting/api/`, `packages/*/Scripting/api/*.binding.toml`, `docs/adr/0044-natives-pass-enums-text-and-wrappers-by-value-and-bindings-use-other-bindings-mirrors.md`

**Status:** ready-for-agent

- [ ] `dotnet test` for Iris.Bindings passes with the new cases.
- [ ] `fenrir-generate-bindings` is a no-op after the commit (CI's api-descriptions check).
- [ ] `grep -l "Fenrir::Math::Vec3\|Fenrir::EntityValue" packages/*/Scripting/api/*.binding.toml packages/FenrirScripting/api/*.binding.toml` lists only `FenrirMath.binding.toml` and the defaults file.

## Comments

Filed from the binding-defaults grilling (spec decisions 3, 4). Last of the stack; when it merges, the commit that records it built deletes `.scratch/binding-defaults/`.
