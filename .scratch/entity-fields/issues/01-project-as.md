# 01: `project-as`: a field holding a projected mirror is a property

**What to build:** the generator projection (spec: "A projected field"). A mirror named in `[csharp] project-as` is stored as itself and shown as the configured C# type.

- **`packages/Iris/Tools/Iris.Bindings/src/BindingConfig.cs`**: `[csharp] project-as`, a table of described type name to `{ type = "<C# type>", get = "<member of the backing field>", set = "<expression>" }` (`set` optional: by default the projected value is assigned, through the mirror's conversion), read into `CSharp.Projections` (a record `Projection(string Type, string Get, string? Set)`); the known-keys list; a per-entry known-keys check as `[bound-types]` has.
- **`src/Binding.cs`**: `Projection(DescribedType)?`, answered through `uses` as `IsReadOnly` is (a used binding's projection is inherited). Validation: an entry that is not a described struct this binding mirrors fails ("`<path>`: project-as names X, which is not a described struct"); a projected mirror held in an array or a fixed buffer fails naming the field ("`<path>`: `<Type>::<field>` is an array of `<X>`, which project-as shows as `<T>`: a projected mirror is held singly").
- **`src/CSharpWriter.cs`** `Fields`: for a member whose struct kind has a projection, emit the doc comment, then `public <T> <Name> { get => <backing>.<get>; set => <backing> = <set>; }` (getter only when the holder is read-only), then `private [readonly] <Mirror> <backing>[ = <initialiser>];` with the backing name the member's C# name in camelCase. The initialiser and `readonly` are what the field would have had. Nothing else about the mirror changes: `Relationship`'s own `readonly struct` shape, the constructor, the mappings and the defaults tests.
- **`README.md`**: the configuration listing shows `project-as`; the "Mirrors" paragraph gains two sentences (a projected field is a property over a private backing field, getter-only on a read-only mirror; it cannot be passed by `ref` or `in`, and cannot be an array); the diagnostics list gains the two refusals.
- **`tests/BindingTests.cs`** with the fixture: a writable mirror holding a projected mirror gets the property with getter and setter and the private backing field with its initialiser; a read-only holder gets a getter-only property and a `readonly` backing field; a used binding's projection is inherited; the not-a-described-struct and array refusals. The fixture gains a `Fixture::Link` struct in `Fixture.hpp` (a `std::uint32_t value = ~0u` and a private `void* m_owner`, under `private-members`, as `Fenrir::Entity` is), held by `Body` (writable) and by the `Touched` event (read-only); `Fixture.binding.toml` projects it as a hand-written `Fixture.Bindings.Id` with `get = "Target"`, whose stand-in lives beside the other hand-written parts in `tests/Fixture/Contract/`; `Fixture.api.json` and the fixture's outputs regenerate.

**Blocked by:** none

**Touches:** `packages/Iris/Tools/Iris.Bindings/src/`, `packages/Iris/Tools/Iris.Bindings/tests/`, `packages/Iris/Tools/Iris.Bindings/README.md`

**Status:** claimed

- [x] Iris.Bindings' tests pass with the new cases; the fixture's committed outputs are regenerated and the fixture assembly compiles (`IrisBindingsFixtureAssembly`).
- [x] No built-in or extension output changes: no binding sets `project-as` yet, so `fenrir-generate-bindings` is a no-op outside the fixture.
- [x] clang-format and `check_tickets.py` pass.

## Comments

Filed 2026-10-28 from the spec. Cut from the spec's branch.
