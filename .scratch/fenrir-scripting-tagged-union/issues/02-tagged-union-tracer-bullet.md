# 02: `[TaggedUnion]` tracer bullet: the generator writes the fixture's union

**What to build:** FenrirScripting gains the `[TaggedUnion]` source generator, end to end for a well-formed union: the author writes a tag enum, the attribute and the private fields, and the generator writes every member of the hand-written pattern. The test fixture's `Shape` is the first union on it (ticket 01's Answer, decisions 1, 2, 4–6).

- **`TaggedUnionAttribute`** in `Fenrir.Scripting`, for structs only, with one optional property, `string? Noun`.
- **`TaggedUnionGenerator`** in `Fenrir.Scripting.Generators`, incremental, with its model kept as plain data like the others (netstandard2.0, Roslyn 4.8). It reads a `[TaggedUnion]` `partial struct`: the tag is the one enum-typed instance field at offset 0, of any underlying type; every other instance field is a kind, paired with the tag member whose name, first letter lowered and prefixed with `_`, is the field's name.
- **It emits into the author's partial:**
  - a parameterless constructor setting the tag to the member with value 0 and that kind's field to `new()`;
  - `public readonly <Tag> Type`;
  - one `[UnscopedRef] public ref <Kind> <Member>` view per kind, calling `Expect` first;
  - one `public static implicit operator <Union>(<Kind> value)` per kind;
  - `private readonly void Expect(<Tag> wanted)`, throwing `InvalidOperationException($"The {noun} is a {_type}, not a {wanted}.")`, where the noun is `Noun`, or the struct's name when it is left out.

  Nothing else (no equality, `ToString` or `Match`). Generated code uses fully qualified names, so it doesn't depend on the author's `using`s.
- **Contract and script assemblies both get it,** through the existing `Fenrir.Scripting.InRepo.props` and `Fenrir.Scripting.props`.
- **`Tests/Fixture/Shapes.cs`:** `Shape` becomes `[TaggedUnion] partial` with only its enum, tag and two fields. `MisplacedShape` and `MisnamedShape` stay hand-written. The C++ side (`ScriptingTests.cpp`, the `int32` tag) is unchanged.
- **README "Tagged unions"** rewritten around the attribute: what the author writes (with the field-name rule and why the offset is written out), what is generated, the hand-written expansion as "what it expands to", `default(T)` being the tag-0 kind with zeroed settings, the `bool` caution, and that the native side stays hand-written and is checked at load.

Diagnostics are ticket 03. Here a malformed union may produce a compiler error from the generated code rather than a FENRIR one.

**Blocked by:** 01 (resolved)

**Status:** resolved (built on branch `claude/tagged-union-tracer-bullet`). Where the build settled something the ticket left open:
- The message test is a new `ScriptingTests` case reading the exact message of the fixture's `Shape` (`Noun = "shape"`) and of a second fixture union, `Solid`, with no noun ("The Solid is a Sphere, not a Box."). The four existing cases are unchanged.
- The compile cases are `TaggedUnionControl` (a `byte` tag and a noun, every member used), `TaggedUnionEmptyKind`, `TaggedUnionNested` (no noun) and `TaggedUnionIntTag`.

- [x] The four existing `ScriptingTests` tagged-union cases pass unchanged with the fixture's `Shape` on the generator (it validates, round-trips, the wrong view throws, a wrong offset or name is refused at load).
- [x] `fenrir_expect_compile` OK cases: `TaggedUnionControl`; an empty kind (one unused byte); a union nested in a partial class; an `int` tag and a `byte` tag; with and without `Noun`.
- [x] A test reads the generated message both with `Noun` and without it.
- [x] The README section describes the attribute and shows the expansion.
