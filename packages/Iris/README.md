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
| `Examples/Hosting` | A small example program, plus an example component and its private dependency. |
| `Tests` | Native and managed checks, run through CTest. |
| `cmake` | .NET host pack discovery. |

## Requirements and build

You need a C++20 compiler, CMake 3.20 or later and the .NET 10 SDK.

```sh
cmake -S packages/Iris -B out/build/iris -DIRIS_BUILD_TESTS=ON
cmake --build out/build/iris --config Debug
ctest --test-dir out/build/iris -C Debug --output-on-failure
```

| Option | Default | Effect |
| --- | --- | --- |
| `IRIS_BUILD_EXAMPLES` | `ON` | Builds `IrisHostingExample`. |
| `IRIS_BUILD_TESTS` | `OFF` | Builds the checks, and the bridge, fixture and example component with `dotnet`. |
| `IRIS_DOTNET_NATIVE_DIR` | empty | Use this exact host pack `native` directory instead of discovering one. |
| `IRIS_DOTNET_TARGET_RID` | empty | Override the target runtime identifier, for example `linux-musl-x64`. |
| `IRIS_DOTNET_EXECUTABLE` | found on `PATH` | The `dotnet` used for pack discovery and test builds. |

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

### Deployment

An application using the library ships:

- the `nethost` shared library next to the executable (the example and checks copy it; on Unix they
  also set an `$ORIGIN`/`@loader_path` run path);
- the bridge's `Iris.Bridge.dll` and a `.runtimeconfig.json` targeting `net10.0`;
- each component with its `.deps.json` and private dependencies (`dotnet publish` produces this);
- an installed .NET 10 runtime, found by nethost or given as `HostConfiguration::dotnetRoot`.

Components reference the bridge with `Private=false` and `ExcludeAssets=runtime`, so they never deploy
a private copy of it.

## Running the example

The test build produces everything the example needs. From the repository root in PowerShell, after
a Visual Studio build:

```powershell
$managed = 'out/build/iris/Tests/managed/Debug'
.\out\build\scripting\Examples\Hosting\Debug\IrisHostingExample.exe "$managed/checks/ManagedChecks.runtimeconfig.json" "$managed/checks/Iris.Bridge.dll" "$managed/component/Iris.Examples.Component.dll"
```

```text
Initialize: ok
Managed bridge initialized; bridge version 9.
Loaded Iris.Examples.Component
Calculations.Add(20, 22) = 42
Calculations.Throw: status 3
System.InvalidOperationException: Component failure — π 🐺.
Release: status 0, unload Pending
Call after release: status 5
```

Pass `--dotnet-root <absolute-directory>` first to choose a .NET installation. In PowerShell, set
`[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)` to display non-ASCII output.

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
    const auto sum = component.InvokeInt32("MyComponent.Calculations", "Add", 20, 22);
    if (sum.Succeeded()) { /* *sum.value == 42 */ }
    (void)component.Release(); // Or let the owner's destructor release it.
}
```

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
- **External serialization.** Calls on one host, or on one owner, must not run concurrently.

### Results and diagnostics

Every managed call returns a `CallResult<T>`: a `status`, a `value` present only on success,
and an owning UTF-8 `diagnostic`.

| Status | Meaning |
| --- | --- |
| `Success` (0) | The call succeeded. |
| `InvalidArgument` (1) | The bridge rejected an argument (a malformed name or path, for example). |
| `Overflow` (2) | Component code threw `OverflowException` (to become `ManagedException` in ticket 07). |
| `ManagedException` (3) | Managed code threw; the diagnostic holds `Exception.ToString()`. |
| `BufferTooSmall` (4) | An output buffer was too small. |
| `InvalidHandle` (5) | The owner was released or moved from, or the bridge did not recognise its handle. |
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
   private dependencies with `AssemblyDependencyResolver`. The bridge is shared with components;
   a component referencing a newer bridge, another culture or another public key is rejected.
5. **Unloading.** Release removes the registry's strong references and calls `Unload`. The registry
   then holds only a long weak reference, which `GetUnloadStatus` reads.

## Checks

| CTest | What it checks |
| --- | --- |
| `ManagedContracts` | The bridge's exports called directly in-process: diagnostic text bounds, component loading, invocation, dependency resolution. |
| `ManagedUnload` | Repeated load/invoke/release cycles collect the context and its assemblies; a retained `Type` keeps unloading pending. Runs alone, with forced GC. |
| `Bridge-real` | The handshake with the real bridge, and rejection of a second bridge file in the same process. |
| `Bridge-fixture` | How the native host handles scripted bridge responses: bounded and invalid diagnostics, malformed results, no repeated calls, string reads, unload observation, cleanup on destruction. |
| `Bridge-legacy`, `-oversized`, `-missing`, `-notbridge` | Rejection of a mismatched version, table size, missing entry, or an assembly without the bridge. |
| `Components` | Component owners against the real bridge: identity, failures, names, paths, moves, release, outliving the host. |

Each check program runs named cases and reports every failure. The fixture bridge
(`Tests/FixtureBridge`) is driven through its component's `InvokeInt32` by scenario names.

## References

- [Native hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting) and [native hosting design](https://github.com/dotnet/runtime/blob/main/docs/design/features/native-hosting.md)
- [hostfxr.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/hostfxr.h), [nethost.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/nethost/nethost.h), [coreclr_delegates.h](https://github.com/dotnet/runtime/blob/main/src/native/corehost/coreclr_delegates.h), [hosting status codes](https://github.com/dotnet/runtime/blob/main/src/native/corehost/error_codes.h)
- [UnmanagedCallersOnly](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.unmanagedcallersonlyattribute)
- [Understanding AssemblyLoadContext](https://learn.microsoft.com/en-us/dotnet/core/dependency-loading/understanding-assemblyloadcontext), [AssemblyDependencyResolver](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.loader.assemblydependencyresolver), [assembly unloadability](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)
- [Runtime identifiers](https://learn.microsoft.com/en-us/dotnet/core/rid-catalog)
- [Coral](https://github.com/StudioCherno/Coral/tree/main), an architectural reference only
