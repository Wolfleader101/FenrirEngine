# Iris

Iris is a C++20 library for hosting .NET (CoreCLR) in a native application: start the runtime, load managed
components into collectible contexts, call into them, and unload them. It has no dependency on
FenrirEngine, which includes it only when `FENRIR_ENABLE_SCRIPTING` is on. See [ADR-0001](../../docs/adr/0001-own-dotnet-hosting-library-before-engine-integration.md)
for the scope, [ADR-0003](../../docs/adr/0003-name-the-hosting-library-iris.md) for the name, and [the implementation plan](IMPLEMENTATION_PLAN.md) for the roadmap.

The public API is frozen as of version 0.2 ([ADR-0006](../../docs/adr/0006-freeze-the-iris-public-api.md)): later
changes add to it, and a breaking change needs its own ADR. Only Windows has been tested so far; see
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
| `cmake` | Host pack discovery, the bridge build, `iris_deploy` and the package config. |

## Requirements and build

You need a C++20 compiler, CMake 3.20 or later and the .NET 10 SDK.

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
| `IRIS_BUILD_TESTS` | `OFF` | Builds the checks and their managed projects. |
| `IRIS_INSTALL` | `ON` when Iris is the top-level project | Generates install rules and the `find_package(Iris)` package. |
| `IRIS_DOTNET_NATIVE_DIR` | empty | Use this exact host pack `native` directory instead of discovering one. |
| `IRIS_DOTNET_TARGET_RID` | empty | Override the target runtime identifier, for example `linux-musl-x64`. |
| `IRIS_DOTNET_EXECUTABLE` | found on `PATH` | The `dotnet` used for pack discovery and managed builds. |

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
find_package(Iris 0.2 CONFIG REQUIRED)    # with CMAKE_PREFIX_PATH including the install prefix
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
Managed bridge initialized; bridge version 21.
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
```

### What crosses the boundary

The C++ types in a signature decide the managed types, and anything else does not compile.

| C++ type | Managed type | Argument | Return | Field / property | Array element |
| --- | --- | --- | --- | --- | --- |
| `bool`, `std::int8_t` … `std::uint64_t`, `float`, `double` | the same primitive | by value | by value | yes | yes |
| `std::string_view`, `std::optional<std::string_view>` (empty = null) | `string` | yes | | `Set` | `Set` |
| `std::optional<std::string>` (empty = null) | `string` | | yes | `Get` | `Get` (of an `Array<std::string>`) |
| a struct with a `StructMapping` | a blittable struct | by value | by value | yes | yes |
| `const Object&` / `std::optional<Object>` | any class or interface | `const Object&` | `std::optional<Object>` | `std::optional<Object>` | |
| `const Array<T>&`, `const Array<T>*` / `std::optional<Array<T>>` | `T[]` | yes (`*` may be null) | yes | | |

Not supported: enums, `char`, generics, delegates, multi-dimensional or jagged arrays, `ref`/`out`
parameters, and passing null for a class parameter. Internal calls (managed code calling native code) use
a different set: primitives other than `bool`, and raw pointers.

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
- **Blittable only.** A managed struct must hold only sized integers, `float`, `double` and other
  blittable structs, with sequential or explicit layout. `bool`, `char`, references, enums and
  automatic-layout structs are rejected with a reason. List every field, in declaration order, using
  public fields or fields you can name; auto-property backing fields do not match.
- **Mapped structs are trivially copyable, standard-layout types.** Their bytes are borrowed for the call
  and copied once on the way back; there is no allocation per call beyond the bridge's return block.
- Padding bytes are not compared. Declare the `StructMapping` before anything uses the struct as an
  argument, return or member, since the library tests for it at compile time.

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
- A `bool` bulk copy trusts the buffer to hold 0 or 1 in each byte.
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
- **Signatures.** Parameters and results are `void`, sized integers, `float`, `double` or pointers. `bool`
  and characters are rejected because their unmanaged representation is ambiguous. A string is passed as
  a UTF-8 `const char*` and a length, a struct as a pointer to it; the pointers are valid for the call.
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
  argument into the component, and one that cannot load fails the query. Attributes on methods and fields
  are not available yet. Metadata text is read-only, so a lone surrogate in a name or attribute string
  becomes U+FFFD rather than failing the whole query (strings passed to and from calls stay strict).
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
(a missing file, unbound internal calls) leaves nothing registered. Iris never triggers a collection, so a
host can observe `Pending` turn into `Collected` but cannot hurry it; an explicit way to wait is a planned
addition.

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
  `GetUnloadState` reports `NotRequested`, `Pending` or `Collected`. The library never forces GC.
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
all registered fails to load, and errors are values. Not available yet: listing a type's members, attributes
on members, an object's runtime type, enums, and forcing a collection.

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
| `ConsumerPackage` | Installs the build, then configures, builds and runs `Examples/Consumer` (a small script host: discovery, objects, strings, structs, arrays, an internal call, the log sink) against it. |
| `UnsupportedTypeDoesNotCompile` | A signature with an unsupported C++ type fails to compile with Iris's own message. |

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

## References

- [Native hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting) and [native hosting design](https://github.com/dotnet/runtime/blob/main/docs/design/features/native-hosting.md)
- [hostfxr.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/hostfxr.h), [nethost.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/nethost/nethost.h), [coreclr_delegates.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/coreclr_delegates.h), [hosting status codes](https://github.com/dotnet/runtime/blob/main/src/native/corehost/error_codes.h)
- [UnmanagedCallersOnly](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.unmanagedcallersonlyattribute)
- [Understanding AssemblyLoadContext](https://learn.microsoft.com/en-us/dotnet/core/dependency-loading/understanding-assemblyloadcontext), [AssemblyDependencyResolver](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.loader.assemblydependencyresolver), [assembly unloadability](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)
- [Runtime identifiers](https://learn.microsoft.com/en-us/dotnet/core/rid-catalog)
- [Coral](https://github.com/StudioCherno/Coral/tree/main), an architectural reference only
