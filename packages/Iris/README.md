# Iris

Iris is a C++20 library for hosting .NET (CoreCLR) in a native application: start the runtime, load managed
components into collectible contexts, call into them, and unload them. It has no dependency on
FenrirEngine, which includes it only when `FENRIR_ENABLE_SCRIPTING` is on. See [ADR-0001](../../docs/adr/0001-own-dotnet-hosting-library-before-engine-integration.md)
for the scope, [ADR-0003](../../docs/adr/0003-name-the-hosting-library-iris.md) for the name, and [the implementation plan](IMPLEMENTATION_PLAN.md) for the roadmap.

Tested on Windows x64 (MSVC, .NET 10), Debug and Release. Linux and macOS builds are supported by the
CMake logic but have not been run yet.

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
find_package(Iris 0.1 CONFIG REQUIRED)    # with CMAKE_PREFIX_PATH including the install prefix
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
- each component with its `.deps.json` and private dependencies (`dotnet publish` produces this);
- an installed .NET 10 runtime, found by nethost or given as `HostConfiguration::dotnetRoot`.

Components reference the bridge with `Private=false` and `ExcludeAssets=runtime`, so they never deploy
a private copy of it.

## Running the example

The example build deploys the bridge and publishes the example component next to the executable, so
it runs without arguments. From the repository root in PowerShell, after a Visual Studio build:

```powershell
.\out\build\iris\Examples\Hosting\Debug\IrisHostingExample.exe
```

```text
Initialize: ok
Managed bridge initialized; bridge version 17.
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

```cpp
using namespace Iris;

Host host;
const auto initialized = host.Initialize({.runtimeConfigPath = "app.runtimeconfig.json",
                                          .bridgeAssemblyPath = "Iris.Bridge.dll"});
if (!initialized.success) { /* initialized.diagnostic explains why */ }

auto loaded = host.LoadComponent("MyComponent.dll");
if (loaded.Succeeded())
{
    Component component = std::move(*loaded.value);
    const auto type = component.FindType("MyComponent.Calculations");
    const auto add = type.value->FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add");
    const auto sum = add.value->Invoke(20, 22);
    if (sum.Succeeded()) { /* *sum.value == 42 */ }
    (void)component.Release(); // Or let the owner's destructor release it.
}
```

### Types

`Component::FindType("My.Namespace.Calculator")` returns a `Type` handle for a public, closed type defined
in the component's assembly (nested types use `+`). `Type::GetInfo()` reports its full and assembly names.

```cpp
const auto type = component.FindType("MyComponent.Calculations");
if (type.Succeeded()) { const auto info = type.value->GetInfo(); /* info.value->fullName */ }
```

A `Type` is a cheap value: copying it is fine and it holds no managed reference. It is valid while its
component is loaded, never reused, and only works with the component it came from; after `Release`
every call returns `InvalidHandle`. It does not keep the component from unloading.

### Methods

`Type::FindMethod<Signature>(name)` finds a public static method by name and exact signature, and
`Method::Invoke(arguments...)` calls it. The managed parameter and return types come from the C++
signature at compile time, so overloads are told apart by their exact parameter types:

```cpp
const auto add  = type.value->FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add");   // int Add(int, int)
const auto addL = type.value->FindMethod<std::int64_t(std::int64_t, std::int64_t)>("Add");   // long Add(long, long)
const auto scale = type.value->FindMethod<double(double, float)>("Scale");
const auto touch = type.value->FindMethod<void()>("Touch");
```

Only `PrimitiveValue` types compile: `bool`, 8/16/32/64-bit integers, `float`, `double`, and `void` for
a return. A method that doesn't match (other name, return type, parameter types or count, generic,
`UnmanagedCallersOnly`, not public, an instance method) is `NotFound`. Any exception the method throws,
including `OverflowException` and `ArgumentException`, is `ManagedException` with its own details; the
status is never derived from the exception's type. A `Method` is a cheap value like `Type`, stops
working (`InvalidHandle`) when its component is released, and does not keep it from unloading.

### Objects

`Type::CreateObject(args...)` constructs an object with the public constructor whose parameters match
exactly (`CreateObject(std::int32_t{5}, 2.5)` calls `T(int, double)`) and returns an `Object` that owns it.
Instance methods are found with `Type::FindInstanceMethod<Signature>` and called on an object:

```cpp
const auto counter = *component.FindType("MyComponent.Counter").value;
auto object = counter.CreateObject(std::int32_t{5});                       // Counter(int)
const auto next = counter.FindInstanceMethod<std::int32_t()>("Next").value;  // virtual int Next()
const auto result = next->Invoke(*object.value);
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
const auto make  = factory.FindMethod<std::optional<Object>(std::int32_t)>("Make").value;  // static Counter Make(int)
const auto total = factory.FindMethod<std::int32_t(const Object&, const Object&)>("Total").value;
auto a = make->Invoke(3);                                  // CallResult<std::optional<Object>>
auto b = make->Invoke(4);
const auto sum = total->Invoke(**a.value, **b.value);      // 7
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
const auto settings = *component.FindType("MyComponent.Settings").value;
const auto count   = *settings.FindField<std::int32_t>("Count").value;         // instance field
const auto enabled = *settings.FindProperty<bool>("Enabled").value;            // instance property
const auto limit   = *settings.FindField<std::int32_t>("StaticCount").value;   // static field
auto object = settings.CreateObject();

(void)count.Set(*object.value, 5);                 // instance: pass the object
const auto n = count.Get(*object.value);           // CallResult<std::int32_t>
(void)limit.Set(41);                               // static: no object
```

`T` is a `PrimitiveValue` matched exactly, or `std::optional<Object>` for a class- or interface-typed
member: `Get` returns a new, independently owned handle (empty for null) and `Set` takes a `const Object*`
(`nullptr` stores null; the object must be an instance of the member's type). A missing, private or
differently typed member, and indexers, are `NotFound`.

Misuse is a diagnostic, not undefined behaviour: using a static member with an object or an instance member
without one, writing a read-only member (a `readonly` field, a constant, a property with no public
setter) and reading a write-only property are `InvalidArgument`. Properties run their accessors, so an
exception from a getter or setter is `ManagedException` with its own details and leaves the object usable.
Like `Type`, a `Member` is a cheap value that holds no managed reference and stops working
(`InvalidHandle`) when its component is released.

### Strings

Strings cross the boundary as UTF-8. Native input is borrowed for the call; a result is an owning copy.

```cpp
const auto strings = *component.FindType("MyComponent.Strings").value;
const auto echo = *strings.FindMethod<std::optional<std::string>(std::optional<std::string_view>)>("Echo").value;

const auto a = echo.Invoke("hello \xCF\x80");     // CallResult<std::optional<std::string>>
const auto b = echo.Invoke(std::nullopt);         // a null string in (for a parameter declared `string?`)...
// b.value is engaged (the call worked) and *b.value is empty: ...a null string out.
```

- **Arguments** are `std::string_view` (never null) or `std::optional<std::string_view>` (empty optional =
  null). Pass `std::string_view` explicitly to `Type::CreateObject`, which deduces its argument types.
- **Returns and members** are `std::optional<std::string>`: an empty optional is a null string, kept apart
  from an empty `std::string`. Members take the same `std::optional<std::string_view>` on `Set`.
- **Decoding is strict.** Malformed UTF-8 in an argument is `InvalidArgument` and no managed code runs. A
  managed string that cannot become UTF-8 (an unpaired surrogate) is `InvalidArgument` on the way back.
  Nothing is replaced with U+FFFD.
- **Any size, one call.** Text may contain null characters and has no fixed buffer size (the only limit is
  2 GiB of UTF-8, in either direction, reported as `InvalidArgument`). A result is produced
  once and copied once; there is no size query that could run managed code twice.
- A string is its own kind: a `string` parameter is matched by `std::string_view` signatures only, never
  by `const Object&`, and an `Object` or integer never matches it.

### Logging

`Host::SetLogSink` registers one process-wide sink for bridge messages (clear it with an empty sink):

```cpp
host.SetLogSink([](const Iris::LogMessage& message) { std::cerr << static_cast<int>(message.level) << ": " << message.text << '\n'; });
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
  `GetUnloadStatus` reports `NotRequested`, `Pending` or `Collected`. The library never forces GC.
- **Handles are never reused.** After release, an owner's calls return `InvalidHandle`; a new load
  gets a new handle.
- **Shared assemblies are permanent.** Assemblies listed in `HostConfiguration::sharedAssemblies` load
  once into the default context and stay shared for the life of the process.
- **External serialization.** Calls on one host, or on one owner, must not run concurrently.

### Results and diagnostics

Every managed call returns a `CallResult<T>`: a `status`, a `value` present only on success,
and an owning UTF-8 `diagnostic`.

| Status | Meaning |
| --- | --- |
| `Success` (0) | The call succeeded. |
| `InvalidArgument` (1) | The bridge rejected an argument (a malformed name or path, for example). |
| `ManagedException` (3) | Managed code threw; the diagnostic holds `Exception.ToString()`. |
| `BufferTooSmall` (4) | An output buffer was too small. |
| `InvalidHandle` (5) | The owner was released or moved from, or the bridge did not recognise its handle. |
| `NotFound` (6) | A lookup found nothing (a type, method or constructor that is missing, not public, generic or abstract). |
| `InUse` (7) | `Component::Release` was refused because the component still has live objects. |
| `NotInitialized` (-1) | The host is not initialized. No managed code ran. |
| `UnexpectedStatus` (-2) | The bridge returned an unknown status. |
| `InvalidResponse` (-3) | The bridge returned a malformed response. |

Managed exception text is at most 4096 bytes, cut on a UTF-8 scalar boundary (`diagnosticTruncated`
says so). If managed code cannot produce valid details, the result keeps its status and a native
fallback message. An unknown status carries only a native message. A failed call is never repeated to build its diagnostic, and it leaves the host
ready for the next call. Native allocation failures still throw normally.

`Initialize` returns an `InitializationResult` instead: `success`, an owning diagnostic that includes
hostfxr's own messages, and flags recording how far the attempt got.

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
   then holds only a long weak reference, which `GetUnloadStatus` reads.

## Checks

| CTest | What it checks |
| --- | --- |
| `ManagedContracts` | The bridge's exports called directly in-process: diagnostic text bounds, component loading, invocation, dependency resolution, shared assemblies. |
| `ManagedUnload` | Repeated load/invoke/release cycles collect the context and its assemblies; a retained `Type` keeps unloading pending. Runs alone, with forced GC. |
| `Bridge-real` | The handshake with the real bridge, and rejection of a second bridge file in the same process. |
| `Bridge-fixture` | How the native host handles scripted bridge responses: bounded and invalid diagnostics, malformed results, no repeated calls, string reads, unload observation, cleanup on destruction. |
| `Bridge-legacy`, `-oversized`, `-missing`, `-notbridge` | Rejection of a mismatched version, table size, missing entry, or an assembly without the bridge. |
| `Components` | Component owners against the real bridge: identity, failures, names, paths, moves, release, outliving the host, shared assemblies. |
| `ConsumerPackage` | Installs the build, then configures, builds and runs `Examples/Consumer` against it. |

Each check program runs named cases and reports every failure. The fixture bridge
(`Tests/FixtureBridge`) is driven by method name (`Scenario:<name>`, `Select:<mode>`, `Counter:<name>`) through the same `FindMethod` and `Invoke`.

## References

- [Native hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting) and [native hosting design](https://github.com/dotnet/runtime/blob/main/docs/design/features/native-hosting.md)
- [hostfxr.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/hostfxr.h), [nethost.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/nethost/nethost.h), [coreclr_delegates.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/coreclr_delegates.h), [hosting status codes](https://github.com/dotnet/runtime/blob/main/src/native/corehost/error_codes.h)
- [UnmanagedCallersOnly](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.unmanagedcallersonlyattribute)
- [Understanding AssemblyLoadContext](https://learn.microsoft.com/en-us/dotnet/core/dependency-loading/understanding-assemblyloadcontext), [AssemblyDependencyResolver](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.loader.assemblydependencyresolver), [assembly unloadability](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)
- [Runtime identifiers](https://learn.microsoft.com/en-us/dotnet/core/rid-catalog)
- [Coral](https://github.com/StudioCherno/Coral/tree/main), an architectural reference only
