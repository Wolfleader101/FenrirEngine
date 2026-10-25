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

**Status:** resolved

- [x] describe-api tests: the fixture's committed description and natives description match byte for byte with the new resource types and the text and `char32_t` native; each new diagnostic (a role on an enum, `std::string_view` by pointer) has a case.
- [x] Iris.Bindings tests: the fixture's outputs match byte for byte; a resource mirror, registration line and defaults case appear per scope and for read-only; `internal-structs` makes an `internal` mirror; `by-hand` drops the registration line; a `[uses]` binding's type is named, not generated, and the generated C# compiles against the stubs and the SDK check; a `string` native declares `ReadOnlySpan<char>`; an enum and a one-field struct declare their types; each new diagnostic has a case.
- [x] `fenrir-describe-api` and `fenrir-generate-bindings` leave the existing packages' outputs unchanged (no selection changed).
- [x] `DescribeApiTests` and `IrisBindingsTests` pass on `msvc-x64-debug`.

## Comments

**How a used binding's descriptions are found (settled).** A binding's configuration did not name its descriptions (the
command line passes them), so `[uses]` could not tell which types a used binding mirrors. A configuration may now name
them, top-level `descriptions = [...]` (relative paths), and a binding listed in another's `uses` must: the tool reads it
over those. The command line's descriptions may then be omitted; when both name them they must be the same files, or
the run fails. `uses` is a top-level list of configuration paths, beside `descriptions`.

**What `[uses]` does (settled).** A type a used binding mirrors (its own mirrors, not its bound types or what it in turn
uses) and this binding's descriptions describe is named by the used binding's C# name, member names and read-only rule;
a mirror or natives file naming one gets a `using` of that namespace; this binding's mappings `#include` the used
binding's mappings header by a path relative to its own (correct in the tree; a port can revisit it if a header moves
onto an include path); it gets no mirror, mapping, registration or defaults case here. A used type that two used
bindings mirror, that this binding excludes or names in `[bound-types]`, that this binding's description selects as its
own (a role other than `nested`), or that is described with another layout, fails. Uses are not transitive: a binding
lists each binding whose types it holds.

**A bound type's `wrapper` (settled).** The spec's `wrapper = "<field>"` would name a member, but the description records
no members of a bound type, and what the binding needs is the wire kind (the natives test's function pointer takes the
field's type; a one-float struct and a float are not passed alike on Windows x64). So the entry gives that kind:
`wrapper = "u32"`. The SDK's generator finds the member itself (ticket 01).

**Resources and registration (settled).** The resource keys are optional: `resource-attribute` and the interface are
required only when the binding mirrors a resource, and a factory only for a resource of that scope and read-only-ness.
`extension.resources` is written only when `[registration]` names a resource factory, so the existing registrations
(which name none) are unchanged. A read-only resource is a `readonly struct`, except a tagged union, which the SDK's
tagged-union generator refuses as readonly; it keeps the read-only interface.

**`internal-structs` and `by-hand` (settled).** `internal-structs` is under `[csharp]`, `by-hand` under `[registration]`.
An internal mirror gets no defaults case: Iris finds public methods only, and a public method cannot return an internal
type; a shared struct's layout is still checked when the extension registers it. A natives test in an assembly other
than the contract that names an internal output type needs the contract's `InternalsVisibleTo` (the fixture compiles
both into one assembly). A public mirror holding an internal one fails.

**Text.** A `const char*` followed by a `std::int32_t` length still declares `string` (the built-in binding's outputs are
unchanged); a `std::string_view` declares `ReadOnlySpan<char>`, with `using System;` in the natives file.

**For the ports.** describe-api records a `char32_t` parameter as `u32`, but Iris's `PrimitiveValue` excludes `char32_t`,
so a native taking one does not register: a port passes the code point as `std::uint32_t` (a field of `char32_t` is fine).
The fixture's used binding (`tests/Fixture/Uses`) binds `Common.api.json`, Spring alone, written from `Fixture.api.json`'s
Spring with the role `shared-struct`, as the package owning it would describe it.
