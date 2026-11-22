# describe-api

Reads the C++ headers a package selects and writes the package's **API description** (ADR-0042): a committed, language-neutral JSON record of the plain-data types it shares with bindings, with each type's layout, field kinds, default values and doc comments, its enums and its tagged unions. A binding's generator reads the description, never the headers, so the package's headers stay the one place a shared type is written by hand. Its second mode describes a binding's native functions from their C++ definitions ("Native functions" below), so a native is written by hand once too.

The tool knows nothing of any binding. It is a .NET console program that uses libclang (the `ClangSharp` and `libclang` NuGet packages, so no LLVM install) to read headers.

## The selection

A package that shares types has `packages/<Package>/api/<Package>.api.toml`:

```toml
# The headers to read, relative to this file.
headers = ["../include/MyPackage/Components.hpp", "../include/MyPackage/Events.hpp"]

# Each shared type, by qualified name, with its role.
[types]
"MyPackage::Body" = "component"
"MyPackage::Touched" = "event"
"MyPackage::Hit" = "shared-struct"
"Math::Vec3" = "bound-type"
```

The roles:

| Role | For |
| --- | --- |
| `component` | A component a binding mirrors. |
| `event` | An event a binding mirrors. |
| `scene-resource` | A resource of a scene a binding mirrors, plain data as a component is. |
| `app-resource` | A resource of the app a binding mirrors, plain data as a component is. |
| `shared-struct` | Plain data native functions take or return by pointer. |
| `data-asset` | A game's data in the asset store (ADR-0075): may hold strings, lists, optionals and paths, and extend one other `data-asset`, whose data members it inherits. No virtual functions. |
| `settings` | An `Install` config or an importer's options (ADR-0084): may hold strings, lists, optionals and paths. |
| `bound-type` | A type every binding has its own of (a vector, a quaternion, an entity handle): recorded by layout, with its scalar components when it is made of them. |
| `enum` | An enum no field holds that is shared all the same (a native function takes it). An enum a field holds is described without being named. |

A component, event, resource or shared struct with private data members is refused unless the selection names it in `private-members`, before `[types]`: its members stay private in C++, but their layout is shared all the same (an entity's value and list, a name's buffer). The name is a selected type's, or a nested type's that a selected type holds (another package's entity, held by a component here); one that is neither selected nor held by a selected type is refused.

```toml
private-members = ["MyPackage::Hit", "Other::Entity"]
```

A bound type may be a class template's specialisation, named by an alias (`"Math::Quat"`) or with its arguments (`"MyPackage::Handle<MyPackage::Mesh>"`); the tool instantiates one the headers never do to read its layout.

A bound type's components are recorded in storage order unless a `[components]` table names another: each entry maps a selected bound type to its component names in the order its components visit and files write them, every component once. Each keeps its own offset, so the layout does not change, nor does any binding. glm stores a quaternion `w` first; files, glTF and C#'s `Quaternion` write it `x` first:

```toml
[components]
"Math::Quat" = ["x", "y", "z", "w"]
```

A type `[types]` does not select as a `bound-type`, or an order that is not a list of names, fails the tool, as does a bound type with no components.

A `[registration]` table is accepted and left alone: it configures `tools/generate-type-registry`, which reads it. Any other key fails the tool.

### Type registrations

A package whose types files read and write (ADR-0074) also gives its selection a `[registration]` table, and `tools/generate-type-registry` writes from the package's description a committed header and source that register those types with FenrirTypes' type registry and specialise `Describe<T>` for each: `header`, `source`, `function` and `includes` say where they go, what the registration function is called and which headers declare the types. The fixture's `tests/Fixture/Fixture.api.toml` has one. `tools/generate-type-registry/README.md` says what the tool writes and how a package joins its `fenrir-generate-type-registry` target.

Types a selected type's fields use (nested structs, tagged unions, enums) are described too, without being listed. A type the fields use that is neither plain data nor a selected bound type fails the tool. A nested struct may hold strings, lists, optionals and paths only when nothing but data assets and settings types reach it (`SCHEMA.md`, "Owning kinds"). The selection says nothing binding-specific: names, namespaces and type mappings for a language belong to its binding.

## The description

`<Package>.api.json` beside the selection, in the schema `SCHEMA.md` documents. It is written only when its content changes. What a file or an inspector needs beyond a type's layout is written in its header as doc commands, each on a line of its own in the doc comment: `@version N`, `@formerly Old::Name`, `@transient` and `@editor-only` on a type, and `@formerly oldName`, `@range min max`, `@step s`, `@display text`, `@group text`, `@readonly`, `@hidden`, `@multiline`, `@units text` and `@transient` (left out of the description) on a field (`SCHEMA.md`, "Type and field doc commands"). The fixture's `tests/Fixture/Fixture.api.json` is an example of every part.

## Running

From a configured build directory:

```sh
cmake --build <build-dir> --target fenrir-describe-api
```

That rebuilds the tool and regenerates every package's description. The ordinary build never runs it. Two generators read the descriptions it writes, so run them after it: `fenrir-generate-bindings` (the bindings, `packages/Iris/Tools/Iris.Bindings/README.md`), then `fenrir-generate-type-registry` (the type registrations, `tools/generate-type-registry/README.md`); `README.md` ("Building and testing") has the line that runs all three in that order, and says how CI (`.github/workflows/api-descriptions.yml`) checks them. Commit the regenerated description, bindings and type registrations with the header change. A package joins the target in its `CMakeLists.txt`:

```cmake
fenrir_describe_api(MyPackage api/MyPackage.api.toml)
```

`cmake/FenrirDescribeApi.cmake` passes the tool the target's include directories and definitions (with those of everything it links) and the project's C++ standard. By hand: `dotnet run --project tools/describe-api/src -- <selection> [compile arguments]`, where `@file` reads arguments from a file, one per line. Except on Windows, the tool borrows clang's own headers (`stddef.h`) from the `clang` on `PATH` unless the arguments give `-resource-dir`.

## Diagnostics

Everything the tool cannot describe fails it, naming the type and member, and every failure is reported in one run. Each line is `<file>:<line>:<column>: error: <Type>[::<member>]: <reason>`, with `(reached through <Type>::<field>)` when the type was described because a field uses it:

| Reason | Cause |
| --- | --- |
| `the default '...' is not one the description records: ...` | A default member initialiser of a form `SCHEMA.md` ("Defaults") does not list, such as a number's that libclang cannot evaluate as a constant. |
| `@<word> is not a doc command (...)` | A doc comment line starting with an `@` word the description does not know. |
| `@<command> is a type's doc command, not a field's`, `... a field's doc command, not a type's`, `@<command>: an enumerator takes no doc command` | A doc command on the wrong subject. |
| `@version takes a whole number from 1: ...`, `@range takes two numbers, ...`, `@<command> is written twice; ...` | A malformed doc command, or a second of one given once. |
| `has no default member initialiser` | A number, enum, pointer or array of them with no initialiser. |
| `cannot tell which component of <T> each argument sets: ...` | A bound type's constructor whose parameters are not named after its components. |
| `[components] names '<name>', which is not one of its components (...)`, `[components] names '<name>' twice`, `[components] leaves out its component '<name>'` | A bound type's `[components]` order that does not name each of its components once. |
| `[components] names an order, but it holds something other than scalars, so it has no components` | A `[components]` order for a bound type that has no `components`. |
| `a private data member outside the tagged-union shape (one enum tag followed by one anonymous union); ...` | A private (or protected) member of a class that is not a tagged union, unless `private-members` names it. |
| `'private-members' names <T>, which [types] does not select and no selected type holds` | A `private-members` entry that is neither a selected type nor a nested type a selected type holds. |
| `an anonymous struct or union outside the tagged-union shape (...)` | The same, for an anonymous member. |
| `a reference member: a shared type holds values, not references` | A reference member. |
| `a virtual function: <a role> is plain data` | A virtual function, a virtual destructor included, in any role. |
| `its base class <B> has data members or virtual functions, which <a role>'s base may not have (...)` | A base class that is not empty, but for a data asset's. |
| `its base class <B> is <a role>: a data asset extends only another data asset`, `... is not selected: ...`, `extends N types: a data asset extends one other data asset at most` | A data asset extending a type the selection does not select as a data asset, or extending more than one. |
| `not trivially copyable: it declares its own copy, move, assignment or destructor, which <a role> may not (...)` | A user-declared copy or move constructor or assignment, or destructor, in any role but `data-asset` and `settings`. |
| `a string, which <a role> cannot hold: the owning kinds (string, list, optional and path) are for data assets and settings types` (and `a list`, `an optional`, `a path`) | An owning kind in a component, event, resource or shared struct, or in a nested struct one of them reaches. |
| `holds a <kind> (<T>::<member>), which <a role> cannot reach, but is reached from <a role> through ... and from <a role> through ...` | A struct holding an owning kind that both a fixed-layout role and a data asset or settings type reach. |
| `its type '<T>' is not a kind the description records` | A member of a type the kinds cannot express (`long double`, `wchar_t`, ...). |
| `a bit-field: a shared type's fields are whole bytes` | A bit-field. |
| `an array of arrays, which the kinds cannot express`, `an array of strings, ...: hold a list` | A multi-dimensional array, or an array of an owning kind. |
| `an optional of an optional, which the kinds cannot express`, `a std::vector<bool>, whose elements are bits rather than values: hold a list of u8`, `a std::basic_string<...>: a string is a std::string of UTF-8 text` | An owning kind the kinds cannot express. |
| `its tag <E> has N values but its union holds M kinds; ...`, `a constructor sets the tag to ...` | A tagged union whose tag values and kinds do not pair in declaration order. |
| `a tagged union needs a default constructor that chooses its kind`, `its default constructor does not hold one kind with that kind's own defaults` | A tagged union whose default is not one kind with its own defaults. |
| `no type of that name in the selected headers`, `an enum takes no role`, `not an enum, so it cannot be selected as one` | A selection naming a type that is not there, an enum with another role than `enum`, or a type that is not an enum as one. |

Errors clang reports reading the headers are printed as clang gives them.

## Native functions

A second mode reads a binding's native functions, the C++ functions its script language calls. They are glue for one binding, not shared types, so they are not in the API description and their configuration lives with the binding: the `[natives]` section of its `<Name>.binding.toml`, of which this tool reads two keys and ignores the rest.

```toml
[natives]
sources = ["../src/PhysicsScripting.cpp"]   # Relative to this file; natives are described in this order.
context = "Fenrir::CallContext"             # What every native takes first.
```

```sh
describe-api --natives <Name>.binding.toml [compile arguments, or @file]
```

It parses each source on its own with the compile arguments and writes `<Name>.natives.json` beside the configuration (again only when its content changes): every free function defined there whose first parameter is `const Context&`, with whether its second is the extension's state (a reference to a non-const class), each later parameter's C++ type and name (a `std::string_view` as the kind `string`, a `char32_t` as a `u32`), its result and its doc comment, in source order. `@param[out]` in a native's doc marks an output, `@param[in,out]` a pointer it reads and writes, `@param[in]` a pointer to one value it reads (never a span, whatever follows it), `@notrunning <expression>` the C++ value a caller gets when the native cannot be called, and `@facade Class.Method` the public static class and method name the binding gives the native as a pass-through of its declaration (the binding's generator writes that method); `@facade Class.Method returns <parameter>` names the one `@param[out]` output that method gives back as its value instead, dropping the native's `bool`. The format is the second half of `SCHEMA.md`.

In CMake, `fenrir_describe_api_natives(<target> <config>)` adds a binding's natives to `fenrir-describe-api`, compiled with `<target>`'s include directories and definitions; `packages/FenrirPhysics/Scripting/CMakeLists.txt` is the example. A native fails the tool, naming the function and parameter, when it has internal linkage (static or in an anonymous namespace: the generated registration names it from another file), takes a reference after the context and state, has an unnamed parameter, a pointer to a pointer, a pointer to a `std::string_view` (text is taken by value) or a type of no kind, documents `@param[out]` or `@param[in,out]` on anything but a pointer to non-const data or `@param[in]` on anything but a pointer to const data, documents a parameter it does not have, documents `@notrunning` but returns nothing, documents `@facade` with anything but `Class.Method` (two identifiers joined by one `.`, followed at most by `returns <parameter>`), documents `@facade` twice, or gives `returns` a name that is not one of its `@param[out]` outputs, beside another single output, or on a result that is neither `bool` nor nothing.

## Tests

`tests/` (xunit) describes the fixture (`tests/Fixture/Fixture.hpp`) and compares the result with the committed `Fixture.api.json` byte for byte, checks a second run rewrites nothing, describes each type in `tests/Fixture/Failing.hpp` alone, expecting its diagnostic, describes the fixture's natives (`tests/Fixture/Natives.cpp`, through `tests/Fixture/Fixture.binding.toml`) against the committed `Fixture.natives.json` (four of them marked `@facade`, on two classes, one with `returns`), and describes `tests/Fixture/FailingNatives.cpp`, expecting each refused native's diagnostic. CTest runs them as `DescribeApiTests` when tests are built; by hand, `dotnet test tools/describe-api/tests`. The fixture is also described by `fenrir-describe-api`, so regenerate it with that target after changing the tool or the fixture.
