# 02: describe-api and Iris.Bindings learn text, resources, internal mirrors and shared bindings

**What to build:** The generators express everything the five hand-written extensions and `Fenrir.Math` need, so a port is a selection and a configuration (spec: "The generators"; ADR-0043, ADR-0044).

- **describe-api** (`tools/describe-api/`):
  - A `std::string_view` parameter is kind `string`, spelled `std::string_view` (`Natives.cs` `SpellValue`, before the `Record` case). `char32_t` is `u32` in fields (`Describer.cs`) and parameters.
  - Selection roles `scene-resource` and `app-resource`, recorded as the type's `role`; the same plain-data rules as a component.
  - `SCHEMA.md` (kinds, roles, the natives `string` kind) and the fixture: a resource of each scope in `Fixture.hpp`, a native taking `std::string_view` and `char32_t` in `Natives.cpp`, regenerated outputs.
- **Iris.Bindings** (`packages/Iris/Tools/Iris.Bindings/`):
  - Natives: `string` → `ReadOnlySpan<char>` over the wire pair `void*, int` (`Natives.cs` `Bind`; the `const char*` + count form stays and declares the same); `enum` by value → the described enum, wire its underlying kind; `struct` by value → when the described struct has one field, or the bound type's entry says `wrapper = "<field>"`, the binding's C# type for it, wire the field's kind; else the existing refusal.
  - Resources: new configuration keys `resource-attribute`, `resource-interface`, `read-only-resource-interface` under `[csharp]`, and `make-scene-resource`, `make-read-only-scene-resource`, `make-app-resource`, `make-read-only-app-resource` under `[registration]`; the mirror (a tagged-union mirror included) carries the attribute and interface; the generated function sets `extension.resources`; `read-only` may name a resource (read-only registration, read-only interface, `readonly struct`); the defaults test gets a case per resource.
  - `internal-structs = [...]`: mirrored `internal`. `by-hand = [...]`: mirror, mapping and test case, but no registration line.
  - `[uses] = ["../../FenrirScripting/api/FenrirMath.binding.toml", ...]`: the tool reads each listed configuration; a type described in a description that binding binds (and does not exclude) is named by that binding's C# name and namespace (a `using` added to the mirror), its mappings header is included by this binding's mappings, and it is not generated, registered or tested here. A type listed in both a `[uses]` binding and this one's `exclude` or `[bound-types]` is a diagnostic.
  - README (configuration, "What it writes", "Diagnostics"), `docs/natives-description.md`, `docs/api-description.md`; the fixture binding and `tests/Checks/Stubs.cs` exercise each new key.

**Blocked by:** 01 (Iris and the SDK pass enums and one-field wrappers by value)

**Touches:** `tools/describe-api/`, `packages/Iris/Tools/Iris.Bindings/`, `packages/Iris/docs/`

**Status:** ready-for-agent

- [ ] describe-api tests: the fixture's committed description and natives description match byte for byte with the new resource types and the text and `char32_t` native; each new diagnostic (a role on an enum, `std::string_view` by pointer) has a case.
- [ ] Iris.Bindings tests: the fixture's outputs match byte for byte; a resource mirror, registration line and defaults case appear per scope and for read-only; `internal-structs` makes an `internal` mirror; `by-hand` drops the registration line; a `[uses]` binding's type is named, not generated, and the generated C# compiles against the stubs and the SDK check; a `string` native declares `ReadOnlySpan<char>`; an enum and a one-field struct declare their types; each new diagnostic has a case.
- [ ] `fenrir-describe-api` and `fenrir-generate-bindings` leave the existing packages' outputs unchanged (no selection changed).
- [ ] `DescribeApiTests` and `IrisBindingsTests` pass on `msvc-x64-debug`.
