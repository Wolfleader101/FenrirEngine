# Iris.Bindings

Generates a C# binding from API descriptions: the C# mirror of every shared struct, tagged union and enum, the Iris
mappings that check those mirrors against the C++ types, the registration lists of the extension that shares them, and a
test that C#'s `new T()` holds C++'s `T{}` defaults; and from a natives description, the C# declarations and the
registration of the extension's native functions, which zeroes each native's outputs before it runs. A shared type is
then written once, in its C++ header, and a native function once, as its C++ definition; everything a binding needs of
them is generated and committed.

The input is one or more **API descriptions** ([`docs/api-description.md`](../../docs/api-description.md)), which a
header reader writes from a package's headers, and one **binding configuration**: the C# side's namespace, directories
and SDK names, and everything else the language-neutral description leaves out. The tool knows nothing of any engine:
its own tests bind a fixture description.

## Running

```sh
dotnet run --project packages/Iris/Tools/Iris.Bindings/src -- <Name>.binding.toml [<Package>.api.json...]
```

The descriptions are those the configuration's `descriptions` names when none are given; when both name them, they must
be the same files. Each output is written only when its content changes. In CMake, `iris_generate_binding()` (from Iris's
`cmake/IrisBindings.cmake`) adds a binding to the bindings target, which the ordinary build never runs:

```cmake
iris_generate_binding(MyPhysics Scripting/api/MyPhysics.binding.toml api/MyPhysics.api.json)
```

```sh
cmake --build <build-dir> --target iris-generate-bindings
```

A parent project names the target by setting `IRIS_GENERATE_BINDINGS_TARGET` before adding Iris (FenrirEngine's is
`fenrir-generate-bindings`). Running it rebuilds the tool and regenerates every binding; commit the result with the
header change, and check in CI that it leaves the tree clean.

## The configuration

`<Name>.binding.toml`, with paths relative to it. [`tests/Fixture/Fixture.binding.toml`](tests/Fixture/Fixture.binding.toml)
uses every key but `registered-namespace` and `descriptions`, which tests set, with the SDK's names in
`tests/Fixture/Host.binding-defaults.toml`; `tests/Fixture/Uses/Common.binding.toml`, the
binding it uses, sets `descriptions`.

```toml
namespace = "My.Physics"               # The C# namespace of the mirrors, and of every registered name.
registered-namespace = "My"            # Optional (namespace): components, events and resources are registered as My.Type instead.
descriptions = ["../../api/MyPhysics.api.json"]  # Optional: the descriptions it binds. A binding another uses names them.
uses = ["../../../MyMath/api/MyMath.binding.toml"]  # Optional: bindings whose mirrors it names (below).
defaults = "../../../MyScripting/api/MyScripting.binding-defaults.toml"  # Optional: the SDK's names, stated once (below).
exclude = ["My::RawHit"]               # Optional: described types this binding leaves out (it binds them in another form).

[csharp]
contract = "../Managed/My.Physics"     # The contract project: its hand-written parts are checked (below).
generated = "../Managed/My.Physics/Generated"  # One mirror per type. The tool owns this directory's .cs files.
usings = ["My.Scripting"]              # Where the SDK names below (and the bound types) live.
component-attribute = "NativeComponent"  # With a component: its attribute and interface (the read-only one for a read-only component).
component-interface = "IComponent"
read-only-component-interface = "IReadOnlyComponent"
event-attribute = "NativeEvent"        # With an event: its attribute and interface.
event-interface = "IEvent"
tagged-union-attribute = "TaggedUnion"  # With a tagged union: its attribute.
resource-attribute = "NativeResource"  # With a resource: its attribute and interfaces.
resource-interface = "IResource"
read-only-resource-interface = "IReadOnlyResource"
pointer-type = "ulong"                 # Optional (ulong): the C# type of an opaque pointer field.
readonly-events = true                 # Optional (false): events as readonly structs with readonly fields.
readonly-structs = ["My::BodyState"]    # Optional: structs C# only reads, mirrored the same way.
private-fields = ["My::Link::m_owner"]  # Optional: fields mirrored private, for hand-written members to reach.
fixed-buffers = ["My::Label::text"]     # Optional: arrays of numbers mirrored as fixed buffers, not [InlineArray] structs.
internal-structs = ["My::ShapeOptions"]  # Optional: structs mirrored internal, for the contract's own use.

[cpp]
mappings = "src/Generated/MyPhysicsMappings.hpp"
includes = ["MyPhysics/Components.hpp", "MyScripting/EngineMappings.hpp"]  # The package's headers, and the bound types' mappings.

[registration]                         # Optional: the extension's registration lists.
source = "src/Generated/MyPhysicsRegistration.cpp"
includes = ["MyScripting/ScriptingExtension.hpp"]
function = "My::Generated::RegisterPhysics"  # void F(<extension>& extension), declared and defined in the source.
extension = "My::ScriptingExtension"
make-component = "My::MakeComponent"   # With a component in the lists: its factory (the read-only one for one in read-only).
make-read-only-component = "My::MakeReadOnlyComponent"
make-event = "My::MakeEvent"           # With an event in the lists.
make-shared-struct = "My::MakeSharedStruct"  # With a shared struct in the lists.
make-scene-resource = "My::MakeSceneResource"  # With a resource of that scope and kind: its factory.
make-read-only-scene-resource = "My::MakeReadOnlySceneResource"
make-app-resource = "My::MakeAppResource"
make-read-only-app-resource = "My::MakeReadOnlyAppResource"
read-only = ["My::Hierarchy", "My::Clock"]  # Components and resources registered (and mirrored) read-only.
by-hand = ["My::Gravity"]              # Optional: registered by the extension itself, so not in the generated lists.

[tests]                                # Optional: the defaults test.
cpp = "Tests/Generated/MyPhysicsDefaultsTests.cpp"
csharp = "Tests/Fixture/Generated/MyPhysicsDefaults.cs"
class = "My.Physics.Tests.Defaults"    # The generated C# class, which the C# test assembly compiles.
includes = ["MyTestApp.hpp"]
setup = "MyTestApp app;"               # The first statement of every test case.
component = "app.Component()"          # A const Iris::Component* holding that class.
tags = "[physics][bindings]"

[natives]                              # Optional: native functions, from <Name>.natives.json beside this file.
sources = ["src/MyPhysicsScripting.cpp"]  # The source reader's (where the natives are defined, in order); not read here.
context = "My::CallContext"            # The source reader's: what every native takes first.
class = "PhysicsNative"                # The internal static partial class declaring them, in the namespace.
attribute = "NativeFunction"           # The SDK's attribute on each declaration.
make = "My::NativeFunction::Make"      # Each registration is make<&F>("Namespace.Class.F").
zero-outputs = "My::ZeroingOutputs"    # A native with outputs is made as make<&zero-outputs<&F, i, j>::Call>(...) (below).
types = { "My::EntityId" = "Entity" }  # The C# type of an alias a native takes, by its C++ name.

[bound-types]                          # The C# type of each bound type, and optionally its constructor's components.
"Math::Vec3" = { type = "Vector3", constructor = ["x", "y", "z"] }
"Math::Quat" = { type = "Quaternion" }  # No constructor: defaults set the components by name, new() { X = 0, ... }.
"My::Handle" = { type = "Handle", default = "Handle.None" }  # Its own default (T{}), when that is not zero bytes.
"My::BodyId" = { type = "BodyId", wrapper = "u32" }  # One member a native passes by value: that member's kind.

[names]                                # Exceptions to the PascalCase rule: Type::member, or a bare member name.
"My::ConvexHullShape::hull" = "Hull"
"aabb" = "Bounds"

[nouns]                                # A tagged union's [TaggedUnion(Noun = ...)].
"My::CollisionShape" = "shape"
```

## What it writes

| Output | Contents |
| --- | --- |
| `<generated>/<Type>.cs` | A `partial struct` per described struct and tagged union, an `enum` per enum. Bound types have their own C# types and get none. |
| `[cpp] mappings` | An `Iris::StructMapping<T>` per struct and tagged union, every field as `Iris::Field<Kind>("Name", offset)`, and an `Iris::EnumMapping<T>` per enum, each with a `static_assert` of the size and alignment the description records (bound types get only the assert: the binding maps them). |
| `[registration] source` | The function that sets the extension's `components`, `events` and `sharedStructs`, each `Make...<T>("Namespace.Type")`, and, when `[registration]` names a resource factory, its `resources`. A type in `by-hand` is in none of the lists. |

A type in `exclude` gets none of these: no mirror, mapping, registration or test case. It is for a type the binding
carries in another form, such as a struct C# holds with a smaller handle than C++'s, which the binding then describes
itself and binds from that description.
| `[tests] cpp` and `csharp` | A Catch2 case per component, event, resource and shared struct, and the C# class with one `T()` method returning `new T()` for each. A mirror in `internal-structs` has none: a public test method cannot return it. Its layout is checked when the extension registers it, unless it is in `by-hand` too, when only the mappings' size assert checks it. |

**Other bindings' types.** A binding in `uses` is read with the descriptions its own `descriptions` names. A type it
mirrors that this binding's descriptions also describe (another package's type a field holds, which a description records
as `nested`) is that binding's: this binding names it by that binding's C# name, adds a `using` of its namespace to the
mirrors and natives that name it, includes its mappings header (by a path relative to this one's) in this one's mappings,
and gives it no mirror, mapping, registration or test of its own. This binding's description may select it too, with
the role the used binding's gives it (a struct a native here fills through a pointer, say), or hold it as `nested`. Its member and enumerator names in defaults are that
binding's too. Bound types are not mirrored, so each binding still names its own in `[bound-types]`.

**Names.** A type's C# name is the last segment of its C++ name, its registered name `Namespace.Type` (a component's
or event's `RegisteredNamespace.Type` when `registered-namespace` is set: a mapping still names the C# type). A field's or
enumerator's is its C++ name with the first letter upper-cased, unless `[names]` says otherwise. A C# field cannot share
its struct's name, so that fails until `[names]` renames one.

**Mirrors.** Fields in storage order, each with its doc comment as `<summary>` (later paragraphs as `<remarks>`) and an
initialiser from its recorded default: `= 1;`, `= 0.05f;`, `= float.PositiveInfinity;`, `= Motion.Simulated;`,
`= new();` for a nested struct or bound type with its own defaults, `= new() { Stiffness = 4 };` when the C++ initialiser
sets some of a nested struct's members, and for a bound type `= new(0.5f, 0.5f, 0.5f);` through its configured
constructor (an integral component as the integer it is), `= new() { X = 0, ..., W = 1 };` by component, or its
configured `default` for its own default. A struct with any default that is not all zero bytes has an explicit
`public T() {}`, so `new T()` runs the initialisers; one whose defaults are all zero (a bound type's own default counts
as zero unless it has a `default`) has bare fields and no constructor, since `default` already holds them, so mirroring
it adds no member C# did not have. A read-only mirror (an event under `readonly-events`, or a struct in
`readonly-structs`) is a `readonly struct` of `readonly` fields, always with the constructor, and a field holding a
read-only struct is `readonly` too. A component gets the component attribute (`[NativeComponent("Namespace.Type")]`) and
interface, an event the event attribute and interface, and a scene or app resource the resource attribute and interface (a
tagged union's mirror too); a resource in `read-only` gets the read-only interface and, unless it is a tagged union, is a
`readonly struct`. A struct in `internal-structs` is `internal`, and a public mirror cannot hold one. An array is an `[InlineArray(N)]` struct nested in its mirror
(`SamplesArray Samples`, with `N` as its `Length` constant), filled by the constructor, since C# cannot initialise one; an array in `fixed-buffers` is a
`fixed` buffer (`public fixed byte Text[256];`) in an `unsafe` mirror instead, which a read-only mirror cannot hold. A
field in `private-fields` is `private`, and its mirror turns off the compiler's unused-field warnings: native code reads
it. A pointer is `pointer-type` at its offset, an empty struct an empty struct.

**Tagged unions.** The mirror holds the storage only, as the scripting SDK's tagged-union generator consumes it:
`[TaggedUnion]`, `[StructLayout(LayoutKind.Explicit)]`, `[FieldOffset(0)] private Tag _type;` and one
`[FieldOffset(unionOffset)] private Kind _kind;` per kind, named `_` and its tag value lowered first. That generator writes
the constructor, which makes the kind whose tag value is 0, so a union whose C++ default is another kind fails.

**The defaults test.** Each case runs `setup`, calls the C# class's method for the type through Iris and compares the
value with C++'s `T{}` through the type's mapping: the bytes of each field at its offset, recursing into nested structs
and array elements, never padding, and of a tagged union only the tag and the kind it selects. A failure names every
field that differs, with both sides' bytes. The C# test assembly compiles the generated class; the C++ test target
compiles the generated source with `[tests] includes` on its include path.

**Natives.** With `[natives]`, the tool also reads the binding's **natives description** (`<Name>.natives.json`,
[its format](../../docs/natives-description.md)), which a source reader writes from the C++ functions themselves, so a
native function is written by hand once, as its C++ definition. It writes:

| Output | Contents |
| --- | --- |
| `<generated>/<class>.cs` | `internal static unsafe partial class <class>` with one `[<attribute>] public static partial R Name(...)` per native, in the description's order, documented from its C++ doc (`@param` and `@return` as `<param>` and `<returns>`). The SDK's generator writes the bodies. |
| `[registration] source` | Also each native's C++ declaration (in its namespace, so the source can name it) and `extension.functions = { <make><&Ns::F>("Namespace.Class.F"), ... }` in the same function, with its `notRunning` expression as the second argument when it has one. A native with outputs is made as `<make><&<zero-outputs><&Ns::F, i, j>::Call>(...)`, naming the indices of its outputs in its full C++ parameter list (the context and state counted), so the host's template zeroes each before the native runs and a native that returns early writes nothing. |

A native's C# declaration drops the leading context and state and maps each C++ parameter as a hand-written declaration
would: an alias in `types` is its C# type; a number or `bool` is itself (`std::int32_t` is `int`, `std::uint64_t` is
`ulong`, a `char32_t` is `uint`); a `std::string_view` (kind `string`) is a `ReadOnlySpan<char>`, which a `string`
converts to; an enum is its described C# enum, crossing as its underlying integer; a wrapper (a described struct of one
field of a number, `bool`, `char16_t` or enum, or a bound type whose `[bound-types]` entry gives its `wrapper` kind) is
its C# type, crossing as that field; `const char*` followed by a `std::int32_t` length is one `string`; any other pointer followed by a
`std::int32_t` count is `T*` (a span C# fixes), unless its doc marks it `@param[in]`, one value the count after it does
not size; a pointer its doc marks `@param[out]` is `out T`, and one it marks
`@param[in,out]` is `T*`; a `void*` or `const void*` is `void*`, an address C# passes as it is; any other `const T*` is
`in T`. `T` is a number, or the C# name of a described type or a `types` alias. An `out T` is an output the registration
zeroes; an output span and a `T*` read and written are not. A result is a number, `bool`, an enum, a
wrapper, `void` or `void*`. Anything else fails naming the function and parameter: a struct by value that is not a
wrapper, an enum or struct nothing describes, a pointer to a type nothing names, a pointer to non-const data that is
neither an output, read and written, nor followed by a count, or any other result (text among them).

The C++ outputs turn clang-format off, so a formatting check passes them as written.

## Defaults

The names of an SDK (its attributes, interfaces, factories, headers, call context and native-function names) are the
same in every binding of that SDK, so they are stated once, in a **defaults file**, `<Name>.binding-defaults.toml`, that
each binding names with `defaults`. The file holds the configuration's own sections with only the keys that name an SDK
thing:

| Section | Keys a defaults file may state |
| --- | --- |
| `[csharp]` | `component-attribute`, `component-interface`, `read-only-component-interface`, `event-attribute`, `event-interface`, `tagged-union-attribute`, `resource-attribute`, `resource-interface`, `read-only-resource-interface`, `pointer-type`, `usings` |
| `[cpp]` | `includes` |
| `[registration]` | `extension`, `make-component`, `make-read-only-component`, `make-event`, `make-shared-struct`, `make-scene-resource`, `make-read-only-scene-resource`, `make-app-resource`, `make-read-only-app-resource`, `includes` |
| `[tests]` | `includes`, `component` |
| `[natives]` | `context`, `attribute`, `make`, `zero-outputs` |

Anything else (a path, a role list, a name of one binding's) fails naming the key. The file's sections are merged into
the binding's before it is read: a section the binding lacks is not added (a binding with no `[registration]` gets none),
a scalar the binding states replaces the default, and a list the binding states comes after the default's entries (the
SDK's includes, then the package's). A binding restating a default, a scalar or a list entry byte for byte, fails with
"already the default from `<file>`; delete it", so a configuration states only what is its own. The generated `using`s
are sorted and never name the file's own namespace, so their order does not depend on which file lists a namespace.
A defaults file has no `defaults` and no `uses` of its own, and a binding whose assembly does not reference the SDK
(a pure math contract, say) names none.

The header reader that writes the natives description reads two keys of a binding's configuration, `[natives] sources`
and `context`, and takes `context` from the defaults file when the binding does not state it; this tool reads every other
key of both files and alone checks what a defaults file may hold.

## Hand-written parts

C# beside a generated mirror is a second `partial` part that adds behaviour only: methods, computed properties,
interfaces, operators. The tool reads every `.cs` file under `[csharp] contract` outside `generated` (and `bin`, `obj`),
and an instance field or auto-property in a part of a generated struct fails the run, naming the file, line, struct and
field: a field there would change the layout the mapping describes. Attributes the mirror already has cannot be added
again; `[nouns]` sets a tagged union's noun.

## Diagnostics

Everything the tool cannot bind fails the run with a message naming the type and field, and nothing is written: an
unknown key in the configuration, a description for another target, a bound type with no `[bound-types]` entry, two
types with one C# name, a tagged union whose default kind is not tag 0 or whose kind is not a struct, a default of a form
the description does not record, a field named as its struct, a hand-written field, an `exclude` naming a type no
description describes or one that a type it binds holds, a `readonly-structs` entry that is not a described struct, a
`private-fields` or `fixed-buffers` entry that is not a field of a described struct, a fixed buffer that is not an array
of numbers or is in a read-only mirror, a component, event, tagged union or resource with no attribute, interface or
(with a registration listing it) factory (each SDK name is needed only by the kind of type that uses it, so a binding
with no components states no component names),
a `read-only` entry that is not a component or resource, a `by-hand` entry that is not a type the registration would
list, an `internal-structs` entry that is not a struct mirrored here or that a public mirror holds, a `wrapper` that is
not a number kind, a type two descriptions describe differently (a struct, tagged union or enum may be described twice, alike, as a binding's
own description holds a package's type; it is bound once, with the role it is selected with), `descriptions` that
differ from the command line's, a `defaults` file that does not exist, is not named `<Name>.binding-defaults.toml`, states
a key a defaults file may not, or gives a default the binding restates, a `uses` entry that does not exist, names no
descriptions or uses itself, a used type that two used bindings mirror, that this binding excludes, names in
`[bound-types]` or selects with another role than the used binding's description does, or that is described with
another layout, and
a native parameter or result it cannot map (above), a `[natives] types` alias no native takes, a `[natives] class`
that is also a mirror's name, or a `[tests]` still naming the natives test (`natives-cpp`, `natives-csharp`,
`natives-class`), which the zeroing registration replaced.

## Tests

`tests/` (xunit, the `IrisBindingsTests` CTest when `IRIS_BUILD_TESTS` is on): the fixture
(`tests/Fixture/Fixture.api.json`, every kind of field, default and type a description records, read from
`Fixture.hpp`) and its natives (`tests/Fixture/Fixture.natives.json`, a copy of describe-api's, every form of native
parameter) give their committed outputs byte for byte, with a resource of each scope and a read-only one, an internal
mirror, a by-hand type, a type of the binding it uses (`tests/Fixture/Uses`, a binding of Spring alone), and natives
taking text, an enum and wrappers; a changed default changes exactly its initialiser; the registration zeroes a
native's output (and both of a native with two) and nothing of a native with inputs only, an output span or a pointer
read and written, and a native added with an output is declared and registered so; running
twice rewrites nothing and a mirror no longer described is deleted; each diagnostic above; the generated C# compiles
against stand-ins for the SDK (`tests/Checks/Stubs.cs`) and, when FenrirEngine's scripting SDK is beside Iris, against it
with its source generators (`tests/Checks/Sdk`). The `BindingDefaults` CTest builds the fixture's C# assembly
(`tests/Checks/Runtime`) and runs the fixture's generated defaults test against it. The fixture is itself a binding, so
the bindings target regenerates its outputs. Its natives' C# class has no bodies without the SDK's generator, so only the
SDK check compiles it, and its registration is never compiled: Iris has no extension to register natives with.
