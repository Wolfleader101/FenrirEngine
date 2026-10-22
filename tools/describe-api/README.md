# describe-api

Reads the C++ headers a package selects and writes the package's **API description** (ADR-0016): a committed, language-neutral JSON record of the plain-data types it shares with bindings, with each type's layout, field kinds, default values and doc comments, its enums and its tagged unions. A binding's generator reads the description, never the headers, so the package's headers stay the one place a shared type is written by hand.

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
| `shared-struct` | Plain data native functions take or return by pointer. |
| `bound-type` | A type every binding has its own of (a vector, a quaternion, an entity handle): recorded by layout only. |

Types a selected type's fields use (nested structs, tagged unions, enums) are described too, without being listed. A type the fields use that is neither plain data nor a selected bound type fails the tool. The selection says nothing binding-specific: names, namespaces and type mappings for a language belong to its binding.

## The description

`<Package>.api.json` beside the selection, in the schema `SCHEMA.md` documents. It is written only when its content changes. The fixture's `tests/Fixture/Fixture.api.json` is an example of every part.

## Running

From a configured build directory:

```sh
cmake --build <build-dir> --target fenrir-describe-api
```

That rebuilds the tool and regenerates every package's description. The ordinary build never runs it; CI runs it and fails when it leaves the tree dirty, so commit the regenerated description with the header change. A package joins the target in its `CMakeLists.txt`:

```cmake
fenrir_describe_api(MyPackage api/MyPackage.api.toml)
```

`cmake/FenrirDescribeApi.cmake` passes the tool the target's include directories and definitions (with those of everything it links) and the project's C++ standard. By hand: `dotnet run --project tools/describe-api/src -- <selection> [compile arguments]`, where `@file` reads arguments from a file, one per line. Except on Windows, the tool borrows clang's own headers (`stddef.h`) from the `clang` on `PATH` unless the arguments give `-resource-dir`.

## Diagnostics

Everything the tool cannot describe fails it, naming the type and member, and every failure is reported in one run. Each line is `<file>:<line>:<column>: error: <Type>[::<member>]: <reason>`, with `(reached through <Type>::<field>)` when the type was described because a field uses it:

| Reason | Cause |
| --- | --- |
| `the default '...' is not one the description records: ...` | A default member initialiser of a form `SCHEMA.md` ("Defaults") does not list. |
| `has no default member initialiser` | A number, enum, pointer or array of them with no initialiser. |
| `cannot tell which component of <T> each argument sets: ...` | A bound type's constructor whose parameters are not named after its components. |
| `a private data member outside the tagged-union shape (one enum tag followed by one anonymous union)` | A private (or protected) member of a class that is not a tagged union. |
| `an anonymous struct or union outside the tagged-union shape (...)` | The same, for an anonymous member. |
| `a reference member: a shared type holds values, not references` | A reference member. |
| `a virtual function: a shared type is plain data` | A virtual function. |
| `its base class <B> has data members or virtual functions` | A base class that is not empty. |
| `not trivially copyable: it declares its own copy, move, assignment or destructor` | A user-declared copy or move constructor or assignment, or destructor. |
| `its type '<T>' is not a kind the description records` | A member of a type the kinds cannot express (`long double`, `wchar_t`, ...). |
| `a bit-field: a shared type's fields are whole bytes` | A bit-field. |
| `an array of arrays, which the kinds cannot express` | A multi-dimensional array. |
| `its tag <E> has N values but its union holds M kinds; ...`, `a constructor sets the tag to ...` | A tagged union whose tag values and kinds do not pair in declaration order. |
| `a tagged union needs a default constructor that chooses its kind`, `its default constructor does not hold one kind with that kind's own defaults` | A tagged union whose default is not one kind with its own defaults. |
| `no type of that name in the selected headers`, `an enum takes no role` | A selection naming a type that is not there, or an enum. |

Errors clang reports reading the headers are printed as clang gives them.

## Tests

`tests/` (xunit) describes the fixture (`tests/Fixture/Fixture.hpp`) and compares the result with the committed `Fixture.api.json` byte for byte, checks a second run rewrites nothing, and describes each type in `tests/Fixture/Failing.hpp` alone, expecting its diagnostic. CTest runs them as `DescribeApiTests` when tests are built; by hand, `dotnet test tools/describe-api/tests`. The fixture is also described by `fenrir-describe-api`, so regenerate it with that target after changing the tool or the fixture.
