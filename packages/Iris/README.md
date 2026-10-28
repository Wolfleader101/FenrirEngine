# Iris

Iris is a C++20 library for hosting .NET (CoreCLR) in a native application: start the runtime, load managed
components into collectible contexts, call into them, and unload them. It has no dependency on
FenrirEngine, which includes it only when `FENRIR_ENABLE_SCRIPTING` is on. See [ADR-0001](../../docs/adr/0001-own-dotnet-hosting-library-before-engine-integration.md)
for the scope, [ADR-0003](../../docs/adr/0003-name-the-hosting-library-iris.md) for the name, and [the implementation plan](IMPLEMENTATION_PLAN.md) for the roadmap.

The public API was frozen at version 0.2 ([ADR-0006](../../docs/adr/0006-freeze-the-iris-public-api.md)). As amended,
releases before 1.0 may break the public headers, and each break is listed under [Changes](#changes) with what it
replaces; 1.0 re-freezes. Only Windows has been tested so far; see
[Platforms](#platforms). Terms such as *component*, *owner*, *handle* and *release* are defined in the
[glossary](../../GLOSSARY.md).

## Layout

| Path | Contents |
| --- | --- |
| `Native` | The C++ library `Iris` (public headers in `include/Iris`). |
| `Managed/Iris.Bridge` | The managed bridge (`net10.0`), resident for the life of the process. |
| `Examples/Hosting` | A small example program, plus an example component, its private dependency and a contracts assembly. |
| `Examples/Consumer` | A standalone project that uses an installed Iris through `find_package(Iris)`. |
| `Tests` | Native and managed checks, run through CTest. |
| `Tools/Iris.Bindings` | The binding generator: C# mirrors, mappings, registration lists and a defaults test from an API description ([README](Tools/Iris.Bindings/README.md)). |
| `docs` | [The API description](docs/api-description.md), the input format the binding generator accepts. |
| `cmake` | Host pack discovery, the bridge build, `iris_deploy`, `iris_generate_binding` and the package config. |

## Requirements and build

You need a C++20 compiler, CMake 3.20 or later and the .NET 10 SDK. The checks (`IRIS_BUILD_TESTS`) also need
Catch2 v3: inside the FenrirEngine repository they use its `libs/catch2` submodule (`git submodule update --init libs/catch2`),
and anywhere else an installed Catch2 that `find_package(Catch2 3)` finds.

```sh
cmake -S packages/Iris -B out/build/iris -DIRIS_BUILD_TESTS=ON
cmake --build out/build/iris --config Debug
ctest --test-dir out/build/iris -C Debug --output-on-failure
```

| Option | Default | Effect |
| --- | --- | --- |
| `IRIS_BUILD_BRIDGE` | `ON` | Builds the managed bridge with `dotnet` (always in Release). |
| `IRIS_PREBUILT_BRIDGE_DIR` | empty | With `IRIS_BUILD_BRIDGE=OFF`: a directory holding a built bridge to deploy and install. |
| `IRIS_BUILD_EXAMPLES` | `ON` when Iris is the top-level project | Builds `IrisHostingExample` and its component. |
| `IRIS_BUILD_TESTS` | `OFF` | Builds the checks (Catch2 test cases) and their managed projects. Needs Catch2 v3, see above. |
| `IRIS_INSTALL` | `ON` when Iris is the top-level project | Generates install rules and the `find_package(Iris)` package. |
| `IRIS_DOTNET_NATIVE_DIR` | empty | Use this exact host pack `native` directory instead of discovering one. |
| `IRIS_DOTNET_TARGET_RID` | empty | Override the target runtime identifier, for example `linux-musl-x64`. |
| `IRIS_DOTNET_EXECUTABLE` | found on `PATH` | The `dotnet` used for pack discovery and managed builds. |
| `IRIS_GENERATE_BINDINGS_TARGET` | `iris-generate-bindings` | The target, not built by default, that `iris_generate_binding()` adds each binding to. |

In FenrirEngine, `FENRIR_ENABLE_SCRIPTING` decides whether the root build adds Iris at all; with it
off, nothing looks for .NET.

### Host pack selection

The native library needs `nethost` and the hosting headers from a .NET SDK host pack:

```text
<dotnet-root>/packs/Microsoft.NETCore.App.Host.<rid>/<version>/runtimes/<rid>/native
```

By default CMake runs `dotnet --list-sdks` and picks the newest complete stable 10.x pack for the
target's RID, printing the choice during configuration. The pack must contain `nethost.h`,
`hostfxr.h`, `coreclr_delegates.h` and the `nethost` library (`nethost.dll` and `nethost.lib` on
Windows, `libnethost.so` on Linux, `libnethost.dylib` on macOS). Cross-compiling requires an
explicit `IRIS_DOTNET_NATIVE_DIR`, and macOS universal builds need one hosting build per
architecture. Set the directory explicitly for reproducible builds.

### Using Iris from another project

Either add the source tree with `add_subdirectory(path/to/Iris)`, or install it and use
`find_package`:

```sh
cmake --install out/build/iris --config Debug --prefix out/install/iris
```

```cmake
find_package(Iris 0.3 CONFIG REQUIRED)    # with CMAKE_PREFIX_PATH including the install prefix
target_link_libraries(MyApp PRIVATE Iris::Iris)
iris_deploy(MyApp)                        # copies nethost and the bridge next to MyApp
```

The install holds the static library and headers, `nethost` (in `bin` with `nethost.lib` on Windows, in
`lib` elsewhere),
the bridge in `share/Iris/bridge`, and the CMake package. `Examples/Consumer` is a complete project
using it; the `ConsumerPackage` check builds and runs it against a fresh install. Build the consumer in
the same configuration as the installed library.

At run time an application needs:

- `nethost` next to the executable (on Unix `iris_deploy` also sets an `$ORIGIN`/`@loader_path` run path);
- `Iris.Bridge.dll`, `Iris.Bridge.runtimeconfig.json` and `Iris.Bridge.deps.json`; the bridge's own
  runtime configuration is a valid `HostConfiguration::runtimeConfigPath`;
- each component with its `.deps.json` and private dependencies (`dotnet publish` produces this), built for
  `net10.0` (the example also sets `EnableDynamicLoading`, which copies NuGet dependencies on build);
- any shared contract assembly listed in `HostConfiguration::sharedAssemblies`, at the path you list;
- an installed .NET 10 runtime, found by nethost or given as `HostConfiguration::dotnetRoot`;
- every internal call a component declares, registered before that component loads (otherwise the load
  fails and names the missing ones).

A component does not need to reference `Iris.Bridge`. If one does (the example component does, to test
resolution), reference it with `Private=false` and `ExcludeAssets=runtime` so it never deploys a copy.

## Running the example

The example build deploys the bridge and publishes the example component next to the executable, so
it runs without arguments. From the repository root in PowerShell, after a Visual Studio build:

```powershell
.\out\build\iris\Examples\Hosting\Debug\IrisHostingExample.exe
```

```text
Initialize: ok
Managed bridge initialized; bridge version 22.
Loaded Iris.Examples.Component
FindType: Iris.Examples.Component.Calculations
FindType(missing): status 6
Calculations.Add(20, 22) = 42
Calculations.Scale(2.5, 4) = 10
Calculations.Throw: status 3
System.InvalidOperationException: Component failure — π 🐺.
Release: status 0, unload Pending
Call after release: status 5
```

It also accepts `[--dotnet-root <absolute-directory>] [runtimeconfig.json bridge.dll component.dll]`.
In PowerShell, set `[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)` to display
non-ASCII output.

## Using the library

`#include <Iris/Iris.hpp>` brings in the whole API (each header also works on its own).

```cpp
#include <Iris/Iris.hpp>
using namespace Iris;

Host host;
const auto initialized = host.Initialize({.runtimeConfigPath = "Iris.Bridge.runtimeconfig.json",
                                          .bridgeAssemblyPath = "Iris.Bridge.dll"});
if (!initialized.Succeeded()) { /* initialized.diagnostic explains why */ }

auto loaded = host.LoadComponent("MyComponent.dll");
if (loaded.Succeeded())
{
    Component component = *std::move(loaded);
    const auto add = component.FindType("MyComponent.Calculations")
                         ->FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add");
    const auto sum = add->Invoke(20, 22);          // find once, call many times
    if (sum.Succeeded()) { /* *sum == 42 */ }
    (void)component.Release();                     // or let the owner's destructor release it
}
```

Every call returns a `CallResult<T>` (see [Results and diagnostics](#results-and-diagnostics)). `*result` and
`result->` reach the value like `std::optional`, so check `Succeeded()` first: dereferencing a failed result is
undefined behaviour (an `assert` in debug builds). **The examples in this guide skip those checks for
brevity**; `Examples/Consumer/main.cpp` shows a host that checks every step. Dereferencing a temporary
result gives the value itself, so `for (const Type& type : *component.GetTypes())` is safe. Failures are
results, not exceptions: Iris throws only when a native allocation or an OS text conversion fails.

The host itself:

```cpp
const auto hostfxr = Host::DiscoverHostFxr();   // where nethost would find hostfxr, without loading it
HostConfiguration configuration{.runtimeConfigPath = "Iris.Bridge.runtimeconfig.json",
                                .bridgeAssemblyPath = "Iris.Bridge.dll",
                                .sharedAssemblies = {"MyGame.Api.dll"}};   // one copy for every component
(void)host.Initialize(configuration);
host.IsInitialized();                           // true once the bridge handshake succeeded

const auto info = component.GetInfo();          // info->assemblyName == "MyComponent"
(void)component.Release();
const auto state = component.GetUnloadState();  // Pending, later Collected
const auto done = component.WaitForUnload(std::chrono::seconds(5));   // forces collections: see "Reloading"
```

### What crosses the boundary

The C++ types in a signature decide the managed types, and anything else does not compile.

| C++ type | Managed type | Argument | Return | Field / property | Array element |
| --- | --- | --- | --- | --- | --- |
| `bool`, `std::int8_t` … `std::uint64_t`, `float`, `double` | the same primitive | by value | by value | yes | yes |
| `char16_t` | `char` (a UTF-16 code unit) | by value | by value | yes | yes |
| an enum with an `EnumMapping` | an enum with the same underlying type | by value | by value | yes | |
| `std::string_view`, `std::optional<std::string_view>` (empty = null) | `string` | yes | | `Set` | `Set` |
| `std::optional<std::string>` (empty = null) | `string` | | yes | `Get` | `Get` (of an `Array<std::string>`) |
| a struct with a `StructMapping` | a blittable struct | by value | by value | yes | yes |
| `const Object&` / `std::optional<Object>` | any class or interface | `const Object&` | `std::optional<Object>` | `std::optional<Object>` | |
| `const Array<T>&`, `const Array<T>*` / `std::optional<Array<T>>` | `T[]` | yes (`*` may be null) | yes | | |

Not supported: generics, delegates, multi-dimensional or jagged arrays, `ref`/`out` parameters, and passing
null for a class parameter. A `bool` is one byte (see below), and `char`, `char8_t`, `char32_t` and `wchar_t` do
not cross on their own, because their managed meaning is ambiguous (`char` and `char8_t` are bytes inside a
mapped struct's fixed-size array). Internal calls (managed code calling native code) use a different set:
primitives including `bool` and `char16_t`, raw pointers, and `std::string_view` parameters.

### Types

`Component::FindType("My.Namespace.Calculator")` returns a `Type` handle for a public, closed type defined
in the component's assembly (nested types use `+`). `Type::GetInfo()` reports its full and assembly names.

```cpp
const auto type = component.FindType("MyComponent.Calculations");
if (type.Succeeded()) { const auto info = type->GetInfo(); /* info->fullName */ }
```

A `Type` is a cheap value: copying it is fine and it holds no managed reference. It is valid while its
component is loaded, never reused, and only works with the component it came from; after `Release`
every call returns `InvalidHandle`. It does not keep the component from unloading.

### Methods

`Type::FindMethod<Signature>(name)` finds a public static method by name and exact signature, and
`Method::Invoke(arguments...)` calls it. The managed parameter and return types come from the C++
signature at compile time, so overloads are told apart by their exact parameter types:

```cpp
const auto add   = type->FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add");   // int Add(int, int)
const auto addL  = type->FindMethod<std::int64_t(std::int64_t, std::int64_t)>("Add");   // long Add(long, long)
const auto scale = type->FindMethod<double(double, float)>("Scale");
const auto touch = type->FindMethod<void()>("Touch");
```

Parameters and returns can be any type in the table above, and `void` for a return. A method that doesn't
match (other name, return type, parameter types or count, generic, `UnmanagedCallersOnly`, not public, an
instance method) is `NotFound`. Any exception the method throws,
including `OverflowException` and `ArgumentException`, is `ManagedException` with its own details; the
status is never derived from the exception's type. A `Method` is a cheap value like `Type`, stops
working (`InvalidHandle`) when its component is released, and does not keep it from unloading.

### Objects

`Type::CreateObject(args...)` constructs an object with the public constructor whose parameters match
exactly (`CreateObject(std::int32_t{5}, 2.5)` calls `T(int, double)`) and returns an `Object` that owns it.
Instance methods are found with `Type::FindInstanceMethod<Signature>` and called on an object:

```cpp
const Type counter = *component.FindType("MyComponent.Counter");
Object object = *counter.CreateObject(std::int32_t{5});                 // Counter(int)
const auto next = counter.FindInstanceMethod<std::int32_t()>("Next");  // virtual int Next()
const auto result = next->Invoke(object);
```

While an `Object` exists the bridge keeps the managed object alive, so it survives GC, and holds only that
one reference; releasing the `Object` (or destroying it) unroots it. A virtual method dispatches to the
override of the object's real type, so a `Next` found on `Counter` runs a subclass's `Next`. An exception
from a constructor is `ManagedException` and roots nothing.

**Release policy.** `Component::Release()` is refused with `InUse` while the component has live objects,
and the component stays loaded and usable; release the objects first. Destroying a `Component` always
force-releases it, so a leaked object can never pin a registration: the objects it still held then return
`InvalidHandle`, as does releasing them. An `Object` may outlive its `Host` and `Component` object and is
destroyed safely in any order. An object only works with methods from its own component (else
`InvalidArgument`) and of a type it is an instance of. See
[ADR-0005](../../docs/adr/0005-component-release-with-live-objects.md) for why release is refused but
destruction is not. Only classes are constructed: a type with no public constructor of the requested
signature, including a struct (which has no explicit parameterless constructor), is `NotFound`.

### Objects as arguments and returns

An `Object` can be passed to a method or constructor by declaring the parameter as `const Object&`, and a
method can return one by declaring `std::optional<Object>`:

```cpp
const auto make  = factory.FindMethod<std::optional<Object>(std::int32_t)>("Make");  // static Counter Make(int)
const auto total = factory.FindMethod<std::int32_t(const Object&, const Object&)>("Total");
auto a = make->Invoke(3);                                  // CallResult<std::optional<Object>>
auto b = make->Invoke(4);
const auto sum = total->Invoke(**a, **b);                  // 7
```

- **Matching.** C++ cannot name the managed type, so an `Object` parameter or return matches any class or
  interface type (not `string` or arrays), and the object's real type is checked when the call is made: an
  object that is not an instance of the parameter's type is `InvalidArgument` and the method does not run.
  Overloads that differ only by such types are ambiguous; looking one up is `InvalidArgument`.
- **Returns.** Each call that returns an object gets a **new, independently owned handle**, even if the
  method returns the same managed object again. Releasing one handle never affects another, and the
  component stays unreleasable until every returned `Object` is released. A `null` return is an empty
  `std::optional`, not an error.
- **Arguments** are rejected before the call if they are released or moved-from (`InvalidHandle`) or belong
  to another component (`InvalidArgument`).

### Fields and properties

`Type::FindField<T>(name)` and `Type::FindProperty<T>(name)` find a public field or property of exactly
type `T`, static or instance (declared or inherited), and return a `Member<T>`:

```cpp
const Type settings = *component.FindType("MyComponent.Settings");
const auto count   = *settings.FindField<std::int32_t>("Count");         // instance field
const auto enabled = *settings.FindProperty<bool>("Enabled");            // instance property
const auto limit   = *settings.FindField<std::int32_t>("StaticCount");   // static field
Object object = *settings.CreateObject();

(void)count.Set(object, 5);                        // instance: pass the object
const auto n = count.Get(object);                  // CallResult<std::int32_t>
(void)limit.Set(41);                               // static: no object
```

`T` is a primitive matched exactly (no widening), a mapped struct, `std::optional<std::string>` for a string
(`Set` takes `std::optional<std::string_view>`), or `std::optional<Object>` for a class- or interface-typed
member: `Get` returns a new, independently owned handle (empty for null) and `Set` takes a `const Object*`
(`nullptr` stores null; the object must be an instance of the member's type). Arrays are not member values
yet. A missing, private or differently typed member, and indexers, are `NotFound`.

Misuse is a diagnostic, not undefined behaviour: using a static member with an object or an instance member
without one, writing a read-only member (a `readonly` field, a constant, a property with no public
setter) and reading a write-only property are `InvalidArgument`. Properties run their accessors, so an
exception from a getter or setter is `ManagedException` with its own details and leaves the object usable.
Like `Type`, a `Member` is a cheap value that holds no managed reference and stops working
(`InvalidHandle`) when its component is released.

### Strings

Strings cross the boundary as UTF-8. Native input is borrowed for the call; a result is an owning copy.

```cpp
const Type strings = *component.FindType("MyComponent.Strings");
const auto echo = *strings.FindMethod<std::optional<std::string>(std::optional<std::string_view>)>("Echo");

const auto a = echo.Invoke("hello \xCF\x80");     // CallResult<std::optional<std::string>>
const auto b = echo.Invoke(std::nullopt);         // a null string in (for a parameter declared `string?`)...
// b.Succeeded() (the call worked) and *b is an empty optional: ...a null string out.
```

- **Arguments** are `std::string_view` (never null) or `std::optional<std::string_view>` (empty optional =
  null). Pass `std::string_view` explicitly to `Type::CreateObject`, which deduces its argument types.
- **Returns and members** are `std::optional<std::string>`: an empty optional is a null string, kept apart
  from an empty `std::string`. Members take the same `std::optional<std::string_view>` on `Set`.
- **Decoding is strict.** Malformed UTF-8 in an argument is `InvalidArgument` and no managed code runs. A
  managed string that cannot become UTF-8 (an unpaired surrogate) is `InvalidArgument` on the way back.
  Nothing is replaced with U+FFFD (type and attribute metadata is the one exception; see below).
- **Any size, one call.** Text may contain null characters and has no fixed buffer size (the only limit is
  2 GiB of UTF-8, in either direction, reported as `InvalidArgument`). A result is produced
  once and copied once; there is no size query that could run managed code twice.
- A string is its own kind: a `string` parameter is matched by `std::string_view` signatures only, never
  by `const Object&`, and an `Object` or integer never matches it.

### Blittable structs

A C++ struct that mirrors a managed blittable struct is declared once with a `StructMapping`, then passes
by value like any other argument, returns by value, and works as a field or property value.

```cpp
struct Vec3 { float X, Y, Z; };

template <>
struct Iris::StructMapping<Vec3>
{
    static constexpr std::string_view typeName = "MyComponent.Vec3";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Vec3, X), IRIS_FIELD(Vec3, Y), IRIS_FIELD(Vec3, Z));
};

const auto add = *math.FindMethod<Vec3(Vec3, Vec3)>("Add");
const auto sum = add.Invoke(Vec3{1, 2, 3}, Vec3{10, 20, 30});   // CallResult<Vec3>
```

- **Validated at lookup.** Each lookup that uses a mapping registers it with the component (repeats are
  cheap and idempotent): the bridge checks the size, each field's name, offset and type, and the field order against the managed
  struct. A mismatch fails the lookup (`InvalidArgument`) with a diagnostic saying what differs, and
  the call can never happen. Structs nested in a mapping are registered first, and are checked too.
- **Blittable only.** A managed struct must hold only sized integers, `float`, `double`, `bool`, `char`,
  enums, fixed-size arrays and other blittable structs, with sequential or explicit layout. References and
  automatic-layout structs are rejected with a reason. List every field, in declaration order, using
  public fields or fields you can name; auto-property backing fields do not match.
- **`bool` is one byte, `char` is two.** In memory, a managed `bool` is one byte, as a C++ `bool` is, and a
  managed `char` is a UTF-16 code unit, as `char16_t` is. Iris copies struct bytes directly (it does not use the
  marshaller), so these match. Managed code is only ever given 0 or 1 for a `bool` that Iris copies: any nonzero byte in a struct,
  a single value or a bulk array copy reads as true, and is stored as 1. (An internal call's `bool` result is
  returned by the native function itself, and a C++ `bool` is always 0 or 1.)
- **Enums.** An enum is declared with an `EnumMapping` (`typeName`), crosses as its underlying integer, and
  works as a method argument or return, a field or property, and a mapped-struct field. The managed enum must
  have exactly the same underlying type (`enum class Mode : std::int32_t` is an `enum Mode : int`); a mismatch
  fails the lookup with a diagnostic. A value the managed enum does not name is carried unchanged.
- **Fixed-size arrays.** A struct field `T name[N]` or `std::array<T, N>` mirrors a C# fixed buffer
  (`fixed byte Text[16]`) or an `[InlineArray(N)]` struct. The element can be anything a struct field can be
  (a number, `bool`, `char16_t`, a mapped struct, an enum), and `char`/`char8_t` elements are bytes (a C# `byte`).
  The length and the element type are validated, and a size mismatch is reported first. This is how a
  `char name[256]` crosses; it is a byte array, not a string, and Iris does no text conversion.
- **Managed names that differ from the C++ members.** `IRIS_FIELD_AS(Transform, pos, "Position")` lists a field under the
  managed field's name, so a C# struct need not copy the C++ naming. Fields are matched by position and then checked
  by name, so only the name given here has to match.
- **Fields by type and offset.** `Iris::Field<float>("X", 0)` declares a field by its C++ type, its managed name and
  its byte offset, without naming the member, so a mapping can cover private members or be generated from a
  description of the type. It takes every type `IRIS_FIELD` does, and gives the same result (both macros expand to
  it). The offset is checked against the managed field like any other, so a wrong one fails with the field named.
- **Opaque pointers.** A struct field of any pointer type (`void*`, `EntityList*`) crosses as eight bytes, unchanged in
  both directions, and Iris never reads through it. The managed field is `nint`, `nuint`, `long` or `ulong` at the
  same offset; any other type fails, naming the field. It is a handle managed code carries, not something it can
  dereference.
- **Empty structs.** `Iris::StructFields()` maps an empty C++ struct to a managed struct with no instance fields,
  both one byte. A mapping with no fields fails against a managed struct that has one (naming it), or when the C++
  struct is not one byte.
- **The struct may live in a shared assembly.** A mapping's `typeName` is looked up in the component's own assembly
  first, then in the assemblies the host shares with every component (a contract). A host that maps engine types
  (a transform, say) defined once in its shared contract validates them against any component, whether or not that
  component's own assembly mentions them. A type in a private dependency of the component is still `NotFound`.
- **Validate up front with `Component::ValidateStruct<T>()`.** It runs the same checks without calling a
  method and returns `CallResult<void>` (`NotFound` for a missing type, `InvalidArgument` with a diagnostic naming
  the field for a mismatch, `InvalidHandle` for a released component). Do this for every mapping when a
  component loads if native code shares struct memory with managed code through pointers, such as an internal
  call, because those never check the layout. Validating again, or using the struct afterwards, is harmless.
- **Mapped structs are trivially copyable, standard-layout types.** Their bytes are borrowed for the call
  and copied once on the way back; there is no allocation per call beyond the bridge's return block.
- Padding bytes are not compared. Declare the `StructMapping` before anything uses the struct as an
  argument, return or member, since the library tests for it at compile time.
- **Comparing two values through a mapping.** `Iris::FieldDifferences(left, right)` (`Iris/FieldDifferences.hpp`)
  returns the fields of two values of a mapped struct that differ, each as `<path> at offset <n>: left <hex>, right
  <hex>` with the path from the mapping's type name, recursing into nested structs and fixed-size array elements and
  never comparing padding. Of a tagged union listed in its optional `std::span<const Iris::UnionLayout>` (the type name,
  and per tag value the index of the field it selects) only the tag and the kind the left value's tag selects are
  compared. The binding generator's defaults test is written over it.

### Bindings generated from a description

Mappings need not be written by hand. [`Tools/Iris.Bindings`](Tools/Iris.Bindings/README.md) reads API descriptions
([the format](docs/api-description.md)), written from a C++ package's headers, and a binding's configuration, and
generates the C# mirror of each shared struct, tagged union and enum, a `StructMapping` per type built with
`Iris::Field<T>` (so private members need no stand-in), the registration lists of the extension that shares them, and a
Catch2 test that C#'s `new T()` holds C++'s `T{}` defaults field by field. From a natives description
([the format](docs/natives-description.md)), written from the C++ native functions themselves, it also generates their C#
declarations, their registration and a test that each writes its outputs when it returns early.
`iris_generate_binding()` adds a binding to a target the ordinary build never runs.

### Arrays

`Array<T>` owns a one-dimensional managed array `T[]`, for `T` a primitive, a mapped struct or `std::string`.
It follows the ownership rules of an `Object`: it keeps the managed array alive, blocks `Component::Release`
(`InUse`) until released, and moves rather than copies.

```cpp
Array<std::int32_t> numbers = *Array<std::int32_t>::Create(component, 4);   // new int[4]
(void)numbers.Set(2, 7);
const auto n = numbers.Get(2);                                    // CallResult<std::int32_t>
const std::int32_t more[] = {10, 20};
(void)numbers.CopyIn(0, more);                                    // bulk copy, primitives and structs
std::int32_t out[4];
(void)numbers.CopyOut(0, out);

const auto sum = *math.FindMethod<std::int32_t(const Array<std::int32_t>&)>("Sum");
const auto total = sum.Invoke(numbers);

const auto range = *math.FindMethod<std::optional<Array<std::int32_t>>(std::int32_t)>("Range");
const auto made = range.Invoke(5);                                // *made is the array, or empty for null
```

- **Elements.** Primitives and mapped structs are read and written by value. A string element reads as
  `std::optional<std::string>` and writes as `std::optional<std::string_view>`, with the string contract
  above. `CopyIn`/`CopyOut` move a range in one call and need primitive or struct elements.
- **Ranges are checked.** An index or range outside the array is `InvalidArgument` with a diagnostic, and
  nothing is read or written.
- **Null versus empty.** A method returning an array is declared `std::optional<Array<T>>`: an empty optional
  is a null array, which is not the same as an array of length 0. A parameter is `const Array<T>&` (never
  null) or `const Array<T>*` (`nullptr` passes null).
- **Shared elements.** An array argument is not copied: the callee sees and can change the same elements.
  A managed method returning the same array twice gives two independently owned handles over shared
  elements, and releasing one leaves the other working.
- **Exact types.** An `Array<double>` never matches an `int[]` parameter, and `object[]` or jagged arrays are
  not supported by signature (`NotFound`). Struct elements are validated like any mapped struct.
- A `bool` bulk copy reads any nonzero byte as true.
- Arrays are not (yet) available as field or property values; hold one in an `Array<T>` and pass it.

### Internal calls (managed code calling native code)

A host exposes native functions to components by registering them; the component reaches them through
static unmanaged function pointer fields.

```csharp
// In the component (needs AllowUnsafeBlocks):
public static unsafe class Natives
{
    public static delegate* unmanaged[Cdecl]<int, int, int> Add;     // bound by name when the component loads
    public static int CallAdd(int a, int b) => Add(a, b);
}
```

```cpp
int AddImpl(int left, int right) { return left + right; }

const auto registered = Iris::RegisterInternalCall<&AddImpl>("MyComponent.Natives.Add");   // "Namespace.Type.Field"
```

- **Register any time.** Registration is process-wide, needs no host, and may happen before or after
  `Host::Initialize`. It applies to components loaded afterwards; a loaded component is not changed.
  Registering the same function under a name again succeeds; a different one is `InvalidArgument`. There
  is no unregistering, so a registered function must stay valid for the life of the process.
- **Checked when the component loads.** The bridge binds every public static unmanaged function pointer
  field in the component assembly by `Namespace.Type.Field` (`Namespace.Outer+Inner.Field` for a nested
  type) and checks the signature. Private and managed function pointers, and fields in dependency assemblies,
  are ignored. Binding runs the declaring type's static constructor, so a static constructor must not call an
  internal call. If any field has no registration, a different signature, a calling convention other than
  `Cdecl`, is `readonly`, thread-static or on a generic type, **the load fails** with `InvalidArgument` and a diagnostic listing every such field. Managed code
  can therefore never call a null pointer. A registration also makes the component require it: a host that
  registers nothing cannot load a component that declares bindings.
- **Shared assemblies bind too.** The public static function pointer fields of an assembly in
  `HostConfiguration::sharedAssemblies` are bound when it is shared, by the same rules and with the same
  diagnostics as a component's, so a shared contract can wrap native calls in friendly C# methods and every
  component reaches the one copy. `Host::Initialize` attaches the registered internal calls before it shares
  anything, so register them before `Initialize`. A field with no registration fails the initialization (`Sharing
  assembly failed: ...` naming the field); nothing is shared then, and registering the function and trying again
  works. A component that keeps a private copy of a contract it does not share does not get that copy bound.
- **Signatures.** Parameters and results are `void`, sized integers, `float`, `double`, `bool`, `char16_t`,
  pointers, enums or wrappers. A struct is otherwise passed as a pointer to it, valid for the call; by value it does
  not compile.
- **Enums.** An enum crosses by value as its underlying integer, and the field declares that integer: a native
  `enum class Gear : std::int32_t` is an `int` in the field, which the C# side casts to and from its own enum.
- **Wrappers.** A struct of one field crosses by value as that field once `Iris::WrapperOf` is specialized for it,
  naming the field's type (a number, `bool`, `char16_t`, an enum or a pointer) and converting both ways. The field
  declares the field's type.

  ```cpp
  struct WindowId { std::uint32_t value; };

  template <>
  struct Iris::WrapperOf<WindowId>
  {
      using Value = std::uint32_t;
      static WindowId From(Value value) { return WindowId{value}; }
      static Value To(WindowId id) { return id.value; }
  };

  bool IsOpen(WindowId window);                  // the field: delegate* unmanaged[Cdecl]<uint, bool>
  ```
- **String parameters.** A parameter (never the result) can be a `std::string_view`: UTF-8 bytes, valid for the
  call, and not checked for being valid UTF-8. The field declares it as two parameters, a `byte*` to the bytes
  and then an `int` length; any other declaration fails the load and the diagnostic shows both signatures.
  A length of zero arrives as an empty view whatever the pointer. A negative length, or a null pointer with a
  length, is refused at the boundary: the function is not called, an Error goes to the log sink, and managed
  code gets a value-initialized result (zero, or null for a pointer).

  ```cpp
  int CountBytes(std::string_view text) { return static_cast<int>(text.size()); }
  Iris::RegisterInternalCall<&CountBytes>("MyComponent.Natives.CountBytes");
  ```

  ```csharp
  public static delegate* unmanaged[Cdecl]<byte*, int, int> CountBytes;   // the bytes, then their length
  ```
- **`bool` and `char` need `[assembly: DisableRuntimeMarshalling]`.** With runtime marshalling on, the runtime
  chooses a function pointer's `bool` and `char` size itself (a four-byte `BOOL`, an encoding), which would not
  match C++. A component with a `bool` or `char` internal call and without the attribute fails to load with a
  diagnostic naming the field. The attribute makes a `bool` one byte and a `char` two.
- **Exceptions stop at the boundary.** The function is wrapped, so a C++ exception never unwinds through
  managed frames. It is reported as an Error through the log sink and managed code receives a
  value-initialized result (zero, or null for a pointer).
- **No lifetime coupling.** A registration holds only a name, kinds and a native address, so it never
  keeps a released component alive.

### Discovering types

These queries are the building blocks for finding scripts; they know nothing about any engine.

```cpp
const auto types = component.GetTypes();                       // CallResult<std::vector<Type>>
for (const Type& type : *types)
{
    const auto base = type.GetBaseType();                       // std::optional<TypeInfo>
    const auto interfaces = type.GetInterfaces();               // std::vector<TypeInfo>, sorted by name
    const auto attributes = type.GetAttributes();               // std::vector<AttributeInfo>
    const auto fields = type.GetFields();                       // std::vector<FieldInfo>
    const auto properties = type.GetProperties();               // std::vector<PropertyInfo>
    const auto methods = type.GetMethods();                     // std::vector<MethodInfo>
    const auto isScript = type.IsAssignableTo("MyContracts.IScript");   // CallResult<bool>
}
```

- **Listing.** `Component::GetTypes()` returns the public, non-generic types the component assembly defines,
  nested public types included, in declaration (metadata) order. These are exactly the types `FindType` can
  find; internal and generic-definition types are not listed. A type that fails to load (a base class in a
  missing assembly, say) is left out and reported to the log sink as a warning. "Declaration order" is the
  order the compiler emitted the types: source order for C#, with a nested type after its enclosing type.
- **Relationships.** `GetBaseType` and `GetInterfaces` are owning snapshots naming types, which need not
  belong to the component (`System.Object`, a contract). Interfaces include inherited ones.
  `IsAssignableTo(Type)` compares two types of the same component. `IsAssignableTo("Full.Name")` resolves a
  name in the component assembly (public types only), then the framework, then the assemblies the component references (so a
  contract assembly works whether it is shared or private); an unresolvable name is `NotFound`.
- **Attributes.** `GetAttributes` lists the attributes on the type itself (not inherited ones) with their
  constructor arguments followed by named arguments. Arguments are read from metadata, so the attribute is
  never instantiated and no component code runs. An argument value is null/unrepresentable
  (`std::monostate`), `bool`, `std::int64_t` (signed integers and enums), `std::uint64_t` (unsigned integers
  and chars), `double`, or `std::string` (a string, or a `Type` argument's full name); `typeName` says what it
  was declared as. Reading attributes loads the assemblies of the attribute types and of any `typeof`
  argument into the component, and one that cannot load fails the query. Metadata text is read-only, so a lone surrogate in a name or attribute string
  becomes U+FFFD rather than failing the whole query (strings passed to and from calls stay strict).
- **Members.** `GetFields`, `GetProperties` and `GetMethods` return owning snapshots, in declaration order, of
  the members **declared on that type** (inherited members are not included: they belong to the base type, so
  walk `GetBaseType` for them). Public and non-public members are listed, each with its `visibility`, and
  compiler-generated members (anything marked `[CompilerGenerated]`, such as auto-property backing fields),
  property and event accessors, constructors, operators and indexers are not. An explicit interface
  implementation is listed under its dotted name. Each has its name, its type (`TypeInfo`) and a `TypeKind` (`Int32`, `Float`,
  `String`, `Enum`, `Struct`, `Object`, `Array`, and so on) for display and serialization, whether it is static, and
  its `attributes` in the `AttributeInfo` shape above (so `[Range(0, 1)]` arrives with its arguments). A field
  is also read-only for `readonly` or `const`, and an instance field of a blittable struct has a byte `offset`
  that is the layout struct-mapping validation checks (empty for anything else). A property reports whether
  it `canRead` and `canWrite` (an accessor exists, whatever its accessibility) and the more accessible
  accessor's visibility (when the two differ, in the order public, protected internal, protected, internal,
  private protected, private). A method has its return type and kind and its parameters.
- **Nothing keeps the component alive.** Snapshots are plain data, and the listed `Type` handles behave
  like those from `FindType`: they stop working (`InvalidHandle`) when the component is released.

### Replacing a component

A component is replaced by releasing it and loading the new build from **its own path**: loading locks the
file on Windows, so build a new version into a new directory rather than overwriting the old one (shadow
copying is not supported). Release everything first: a live `Object` or `Array` makes `Component::Release`
return `InUse`.

```cpp
{ Component a = *host.LoadComponent("v1/MyComponent.dll"); /* use it */ }   // released on destruction
Component b = *host.LoadComponent("v2/MyComponent.dll");                    // new behaviour
```

Handles from the old version (`Type`, `Method`, `Member`, `Object`, `Array`, the owner) report `InvalidHandle`
after it is released, and are never reused, so they cannot select anything in the new version. Unloading is
cooperative: `GetUnloadState` reports `Pending` until nothing in the host keeps the old code alive (a
`Type` object you kept from `System.Reflection`, a thread, a static) and `Collected` afterwards. Iris keeps no
root of its own that the tests have found: handle tables, internal call bindings, type listings and attribute
snapshots are plain data, and components that used all of them were observed to collect. A failed load
(a missing file, unbound internal calls) leaves nothing registered.

To confirm that the old version is gone before loading the new one, call
`component.WaitForUnload(timeout)` on the released owner. It returns `Collected`, or `Pending` if the timeout
passed first (nothing is lost, and the host stays usable). A `Pending` result after a generous timeout means a
root is still holding the component: a running thread, a static that holds one of its types, or an object a host
kept. A loaded or already collected component returns at once.

- **It forces collections, and that is its cost.** Each attempt while the component is `Pending` runs a full,
  blocking garbage collection with finalizers on the whole runtime, then looks again every few milliseconds
  until the timeout; a timeout of zero still runs one collection if the component is `Pending`. That pauses
  every managed thread, so it is only for reload points, never per frame. Nothing
  else in Iris collects; if you never call it, `Pending` still turns into `Collected` whenever the runtime next
  collects.
- **Why no `Host::CollectGarbage`.** A bare collection answers nothing the host needs, since the question at a
  reload is whether this component is gone, and the wait asks exactly that.

### Logging

`Host::SetLogSink` registers one process-wide sink for bridge messages (clear it with an empty sink). It needs
an initialized host, so messages from `Initialize` itself are not delivered:

```cpp
(void)host.SetLogSink([](const Iris::LogMessage& message) {
    std::cerr << static_cast<int>(message.level) << ": " << message.text << '\n';
});
```

Messages arrive with a `LogLevel`. Bridge events (a component loaded or released, an assembly shared) are
`Info`. A managed exception caught at a bridge boundary is reported with its full `ToString()`: `Error` for
exceptions from component code, `Warning` for the bridge's own argument validation. The call that raised
it still returns the same details in its result. A lookup that finds nothing (`NotFound`) is not an
exception and is not logged. The sink runs synchronously on the calling thread and must be safe to run concurrently if calls come
from several threads. It may throw (the exception is swallowed) or clear itself; messages raised by calls it
makes into Iris are dropped, and it stays registered after the host that set it is destroyed, so what it
captures must outlive it. `text` is only valid during the call.

### Lifetime rules

- **Process-lifetime runtime.** Constructing `Host` does not start .NET; `Initialize` starts or
  joins CoreCLR. CoreCLR, hostfxr and the bridge stay loaded until the process exits, whatever happens
  to the host.
- **One bridge per process.** The bridge loads into the default load context. Initializing again with
  the same bridge file works; a different bridge file fails.
- **Owners outlive the host.** A `Component` keeps working after its `Host` is reset or
  destroyed. Destroy owners before `main` returns, never during static destruction.
- **Release is not collection.** `Release` (or destroying the owner) drops the bridge's strong
  references and requests unloading. Collection happens later, when nothing else roots the component.
  `GetUnloadState` reports `NotRequested`, `Pending` or `Collected`. The library forces a collection only inside
  `WaitForUnload`, when the host asks.
- **Handles are never reused.** After release, an owner's calls return `InvalidHandle`; a new load
  gets a new handle.
- **Shared assemblies are permanent.** Assemblies listed in `HostConfiguration::sharedAssemblies` load
  once into the default context and stay shared for the life of the process.
- **External serialization.** Calls on one host, or on one owner, must not run concurrently (see
  [Threading](#threading)).

### Results and diagnostics

Every managed call returns a `CallResult<T>`: a `status`, a `value` present only on success,
and an owning UTF-8 `diagnostic`.

| Status | Meaning |
| --- | --- |
| `Success` (0) | The call succeeded. |
| `InvalidArgument` (1) | A value could not cross the boundary, or the call was misused: a malformed or ambiguous name, malformed UTF-8 in, a returned string that cannot become UTF-8 or exceeds 2 GiB, an index out of range, a mismatched struct mapping, an object of the wrong type or component, writing a read-only member. |
| `ManagedException` (3) | Managed code threw; the diagnostic holds `Exception.ToString()`. |
| `InvalidHandle` (5) | The owner was released or moved from, or the bridge did not recognise its handle. |
| `NotFound` (6) | A lookup found nothing (a type, method or constructor that is missing, not public, generic or abstract). |
| `InUse` (7) | `Component::Release` was refused because the component still has live objects. |
| `NotInitialized` (-1) | The host is not initialized (or the result was never filled in). No managed code ran. |
| `UnexpectedStatus` (-2) | The bridge returned an unknown status. |
| `InvalidResponse` (-3) | The bridge returned a malformed response. |

Managed exception text is at most 4096 bytes, cut on a UTF-8 scalar boundary (`diagnosticTruncated`
says so). If managed code cannot produce valid details, the result keeps its status and a native
fallback message. An unknown status carries only a native message. A failed call is never repeated to build its diagnostic, and it leaves the host
ready for the next call. Native allocation failures still throw normally.

`Initialize` returns an `InitializationResult` instead: `Succeeded()`, an owning diagnostic that includes
hostfxr's own messages, and flags recording how far the attempt got.

There is deliberately no conversion from `CallResult` to `bool`: `if (type.IsAssignableTo("X"))` would test
whether the call worked, not the answer. Write `Succeeded() && *result`.

### Threading

- **Required:** calls on one `Host`, and on one owner (`Component`, `Object`, `Array`), must be serialized
  by the caller. Handles (`Type`, `Method`, `Member`) are immutable values and may be copied across threads.
- **Designed, not verified:** calls on *different* owners from different threads. The bridge guards its
  tables with process-wide locks, taken briefly around each lookup and never while component code runs, and
  the native process-wide tables (internal calls, the log sink) are mutex-guarded, but no test runs Iris
  concurrently.
- **Your responsibility:** the log sink and internal call functions run on whatever thread calls them, which
  includes threads component code starts. CoreCLR attaches native threads automatically; there is no
  attach call.

Until the engine integration (ticket 19) decides otherwise, drive Iris from one thread.

### Coming from Mono or Coral

| You want to | Mono | Coral | Iris |
| --- | --- | --- | --- |
| Start the runtime | `mono_jit_init` | `HostInstance::Initialize` | `Host::Initialize` |
| Load and unload code | domains | `AssemblyLoadContext` | `Host::LoadComponent`, `Component::Release` |
| Find a type | `mono_class_from_name` | `GetType` | `Component::FindType`, `GetTypes` |
| Call a method | `mono_runtime_invoke` | `InvokeMethod("Name", ...)` | `FindMethod<Sig>` once, then `Invoke` |
| Create an object | `mono_object_new` | `CreateInstance` | `Type::CreateObject` (an RAII `Object`) |
| Read a field | `mono_field_get_value` | `GetFieldValue` | `FindField<T>`, then `Member<T>::Get` |
| Native code callable from C# | `mono_add_internal_call` | `AddInternalCall` | `RegisterInternalCall<&Fn>` |
| Handle errors | exception out-parameter | exception callback | `CallResult` status, plus the log sink |

What Iris does differently: signatures are matched exactly at lookup (and unsupported types fail to
compile), struct layouts are checked against the managed type, a component whose internal calls are not
all registered fails to load, and errors are values. Not available yet: an object's runtime type, and forcing a
collection.

## How it works

1. **Discovery and loading.** nethost locates hostfxr, which is loaded once per process and never
   unloaded. A different hostfxr in the same process is rejected.
2. **Runtime start.** `hostfxr_initialize_for_runtime_config` prepares a temporary context; the host
   obtains the `load_assembly` and `get_function_pointer` delegates, then closes the context. Joining a
   runtime started with different properties is rejected.
3. **Bridge handshake.** The bridge loads into the default load context. Its one public entry point,
   `Bootstrap.GetBridgeFunctions`, fills a versioned table of function pointers after checking the
   caller's version and table size. See
   [ADR-0002](../../docs/adr/0002-bridge-in-default-load-context-with-function-table.md).
4. **Components.** Each `LoadComponent` creates a collectible `AssemblyLoadContext` that resolves
   private dependencies with `AssemblyDependencyResolver`.
5. **Shared assemblies.** The bridge, and any assembly in `HostConfiguration::sharedAssemblies`, is
   loaded once into the default context. A component's reference to one always binds to that copy,
   even if the component deploys its own, so types such as a scripting API keep one identity across
   components. A reference the shared copy cannot satisfy (a newer version, another culture or public
   key) is rejected. See [ADR-0004](../../docs/adr/0004-shared-contract-assemblies.md).
6. **Unloading.** Release removes the registry's strong references and calls `Unload`. The registry
   then holds only a long weak reference, which `GetUnloadState` reads.

## Checks

| CTest | What it checks |
| --- | --- |
| `ManagedContracts` | The bridge's exports called directly in-process: diagnostic text bounds, component loading, invocation, dependency resolution, shared assemblies. |
| `ManagedUnload` | Repeated load/invoke/release cycles collect the context and its assemblies; a retained `Type` keeps unloading pending. Runs alone, with forced GC. |
| `Bridge-real` | The handshake with the real bridge, and rejection of a second bridge file in the same process. |
| `Bridge-fixture` | How the native host handles scripted bridge responses: bounded and invalid diagnostics, malformed results, no repeated calls, string reads, unload observation, cleanup on destruction. |
| `Bridge-legacy`, `-oversized`, `-missing`, `-notbridge` | Rejection of a mismatched version, table size, missing entry, or an assembly without the bridge. |
| `Components` | Every public feature against the real bridge: owners, types, methods, objects, members, strings, structs, arrays, internal calls, discovery, replacement, release, outliving the host, shared assemblies. |
| `Components-unbound` | The same program with no internal calls registered: loading a component that declares them fails and names them. |
| `Fake` | The native modules against a scripted bridge function table that lives in the test process (`Tests/Fake/FakeBridge.hpp`), with no .NET runtime: how a response the bridge gives is handled, malformed ones included, and that every block it hands out is given back (type snapshots, strings, handles, arrays, statuses and diagnostics). |
| `FieldDifferences` | `Iris::FieldDifferences` over a mapped struct (a scalar, a nested struct, a fixed-size array, an enum, padding) and a tagged union, with no .NET runtime. |
| `Numbering-native` | The numbers the native side gives to value kinds, snapshot type kinds, visibilities, statuses, log levels and unload states, against the one table both sides share (`Tests/numbering.txt`). The managed side reads the same table in the "numbering" group of `ManagedContracts`. |
| `ConsumerPackage` | Installs the build, then configures, builds and runs `Examples/Consumer` (a small script host: discovery, objects, strings, structs, arrays, an internal call, the log sink) against it. |
| `UnsupportedTypeDoesNotCompile` | A signature with an unsupported C++ type fails to compile with Iris's own message. |
| `IrisBindingsTests` | The binding generator against its fixture: the committed outputs byte for byte, its diagnostics, and the generated C# compiled against stand-ins for the scripting SDK (and the SDK itself, when it is beside Iris). |
| `BindingDefaults` | The fixture's generated defaults test, run: each registered type's C# `new T()` against C++'s `T{}`, field by field through the generated mappings. |

Each check program runs named cases and reports every failure. The fixture bridge
(`Tests/FixtureBridge`) is driven by method name (`Scenario:<name>`, `Select:<mode>`, `Counter:<name>`) through the same `FindMethod` and `Invoke`.

## Platforms

| Platform | Compiler | Configure and build | Tests | Notes |
| --- | --- | --- | --- | --- |
| Windows 11 x64 | MSVC 19.44 (Visual Studio 2022) | verified | all 12 pass, Debug and Release | .NET SDK 10.0.401 |
| Linux x64 | GCC / Clang | not run | not run | CMake logic exists (`libnethost.so`, `$ORIGIN` run path) |
| macOS (arm64, x64) | Apple Clang | not run | not run | CMake logic exists (`libnethost.dylib`, `@loader_path`); one build per architecture |

Release is verified by running `ctest -C Release` by hand; CMake does not enforce it. There is no CI yet:
automated runners for all three platforms are future work.

## Changes

Each release lists every public change and what it replaces, as ADR-0006 (amended) requires before 1.0.

### 0.3.0

- **Added `Iris::Field<T>(name, offset)`**, a public way to declare a mapped field by C++ type and byte offset with no
  member access. `IRIS_FIELD` and `IRIS_FIELD_AS` are unchanged for callers and now expand to it; it replaces
  `Iris::Detail::MakeField`, which they used to expand to (not API, now gone).
- **Added the opaque pointer field kind**: a pointer member of a mapped struct maps to a managed `nint`, `nuint`, `long`
  or `ulong`. It replaces hand-written stand-in structs that put a 64-bit integer where the pointer is.
- **Added zero-field structs**: `Iris::StructFields()` with no arguments maps an empty struct. A managed struct with
  no fields used to be refused ("has no fields"); this replaces a private placeholder field added to make it map.
- **Added `Iris::FieldDifferences`** (`Iris/FieldDifferences.hpp`, with `Iris::UnionLayout` and `Iris::UnionKind`): the
  fields of two values of a mapped struct that differ, walked through the mapping. It replaces the comparison the
  binding generator used to write into every defaults test over `Iris::Detail`'s mapping record (not API).
- **Added the binding generator** (`Tools/Iris.Bindings`, `iris_generate_binding()` in `cmake/IrisBindings.cmake`): C#
  mirrors, mappings, registration lists and a defaults test generated from an API description. It replaces writing
  each of those by hand; the public headers do not change.
- **The binding generator binds an existing contract unchanged**: `exclude`, `readonly-structs` and a bound type's
  `default` in the configuration, and a mirror whose defaults are all zero has no constructor. These replace the
  surface a generated mirror would otherwise add or change (a constructor, writable state, an all-zero entity link),
  so a hand-written contract can move to generated mirrors with its public API as it was.
- **The binding generator writes native functions** (`[natives]` in the configuration, read from a natives description,
  `docs/natives-description.md`): their C# declarations, their registration (each declared in the registration source)
  and a test that each native with an output writes its default when it returns early. It replaces declaring each
  native in C# and listing it in the extension by hand; the public headers do not change.
- **The binding generator binds a contract whose names and fields are its own**: `registered-namespace` (components and
  events registered under another namespace than the C# one), `[csharp] private-fields` and `fixed-buffers`, and natives
  that take or return a `void*`, take a pointer they read and write (`[in,out]`) or give a value when they cannot be
  called (`@notrunning`). These replace the hand-written mappings, registrations and declarations a contract needed
  where a generated one would have changed its public API; the public headers do not change.

## References

- [Native hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting) and [native hosting design](https://github.com/dotnet/runtime/blob/main/docs/design/features/native-hosting.md)
- [hostfxr.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/hostfxr.h), [nethost.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/nethost/nethost.h), [coreclr_delegates.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/coreclr_delegates.h), [hosting status codes](https://github.com/dotnet/runtime/blob/main/src/native/corehost/error_codes.h)
- [UnmanagedCallersOnly](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.unmanagedcallersonlyattribute)
- [Understanding AssemblyLoadContext](https://learn.microsoft.com/en-us/dotnet/core/dependency-loading/understanding-assemblyloadcontext), [AssemblyDependencyResolver](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.loader.assemblydependencyresolver), [assembly unloadability](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)
- [Runtime identifiers](https://learn.microsoft.com/en-us/dotnet/core/rid-catalog)
- [Coral](https://github.com/StudioCherno/Coral/tree/main), an architectural reference only
