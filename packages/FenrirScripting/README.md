# FenrirScripting

Optional, independent C++/.NET hosting package. The native library has no dependency
on ECS, rendering, the editor, or other Fenrir modules.

The current step discovers and loads `hostfxr`, prepares a hosting context using
a `.runtimeconfig.json`, obtains the assembly-loading runtime delegate, and closes
the context. Obtaining the delegate starts or reuses CoreCLR. The bridge is not
loaded; `IsInitialized` remains false and full initialization reports bridge loading
as pending.

## Select the target hosting SDK files

Enabled native builds automatically find the newest complete stable .NET 10 host
pack for the C++ target platform and architecture. CMake locates `dotnet` (with
`DOTNET_ROOT` as a hint), calls `dotnet --list-sdks`, and searches the reported
installation roots. It prints the selected pack path during configuration. This
major version matches the current bridge's `net10.0` target.

Set `FENRIR_DOTNET_NATIVE_DIR` to select an exact pack instead, bypassing discovery:

```text
<dotnet-root>/packs/Microsoft.NETCore.App.Host.<rid>/<version>/runtimes/<rid>/native
```

For example, this machine has the Windows x64 pack at:

```text
C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Host.win-x64/10.0.12/runtimes/win-x64/native
```

Automatic RID selection supports Windows, Linux with glibc, and macOS on common
x86/ARM targets. Use `FENRIR_DOTNET_TARGET_RID` for other variants, such as
`linux-musl-x64` on Alpine. Override `FENRIR_DOTNET_EXECUTABLE` to choose another
SDK installation. Cross-compilation requires an explicit native directory for the
target's pack. macOS universal builds require separate hosting builds per architecture.

An explicit native directory remains cached until changed. Clear it with
`-DFENRIR_DOTNET_NATIVE_DIR=` to restore automatic selection. Auto-selected paths
are not pinned in the cache; use an explicit directory for reproducible CI builds.
No hosting headers or platform binaries are copied into the source tree.

Every directory must contain `nethost.h`, `hostfxr.h`, `coreclr_delegates.h`, and the
shared native dependency:

| Target | Required library files |
| --- | --- |
| Windows | `nethost.dll` and its import library `nethost.lib` |
| Linux | `libnethost.so` |
| macOS | `libnethost.dylib` |

These headers and native loader details remain private to the implementation.
`nethost.h` declares discovery; `hostfxr.h` supplies the precise export signatures
and calling conventions. `coreclr_delegates.h` supplies the assembly-loading
delegate's exact signature and calling convention. Use the headers from the selected SDK pack.

## Build the native library and example

From the repository root, with a C++20 compiler, CMake 3.20 or later, and .NET 10 SDK:

```sh
cmake -S packages/FenrirScripting -B out/build/scripting
cmake --build out/build/scripting --config Debug --target FenrirHostingExample
```

The package defaults to enabled when configured directly. In the engine root build,
use `-DFENRIR_ENABLE_SCRIPTING=ON` to enable it or `OFF` to disable it. Set
`FENRIR_SCRIPTING_BUILD_EXAMPLES=OFF` to build only the native library.
With scripting disabled, CMake skips the dependency module entirely: no SDK
checks, .NET discovery, or `dotnet` invocation occur.

The example copies `nethost` beside its executable; Unix builds also add a local
runtime search path. Other executables consuming this static host library must
deploy the same native dependency themselves. Production installation/packaging
is outside this step.

Run the example from `out/build/scripting/Examples/Hosting`. Multi-configuration
generators such as Visual Studio place it in the `Debug` subdirectory. The executable
is named `FenrirHostingExample` (`FenrirHostingExample.exe` on Windows).

Build the managed bridge as described below, then run the example from the
repository root, passing its generated runtime configuration. For Windows PowerShell
with a Visual Studio build:

```powershell
.\out\build\scripting\Examples\Hosting\Debug\FenrirHostingExample.exe packages/FenrirScripting/Managed/Fenrir.ManagedHost/bin/Debug/net10.0/Fenrir.ManagedHost.runtimeconfig.json
```

Expected output and exit code zero (the path depends on the installed runtime and platform):

```text
Initialized before discovery: false
Hostfxr discovered: true
Initialized after discovery: false
Hostfxr path: <absolute path to hostfxr>
Hostfxr loaded: true
Required exports resolved: true
Runtime configuration initialized: true
Runtime delegate obtained: true
Host context closed: true
Initialization succeeded: false
Diagnostic: Runtime delegate obtained and host context closed; managed bridge loading is not implemented yet.
Initialized after attempt: false
```

The example accepts:

```text
FenrirHostingExample [--dotnet-root absolute-directory] runtimeconfig.json [bridge.dll]
```

An omitted .NET root uses `nethost`'s environment/registered-installation search.
An explicit absolute root guides initial discovery. Once hostfxr is loaded,
`nethost` may return that already loaded library regardless of the specified root.
A relative root is rejected by the wrapper.
For example, in a fresh process an absolute nonexistent root should print `Hostfxr discovered: false`,
an owning diagnostic with a hexadecimal native status, and exit with code one.
Discovery failure leaves the wrapper uninitialized.

`FENRIR_DOTNET_NATIVE_DIR` selects build-time files. `--dotnet-root` guides discovery
and is also passed to context initialization for framework resolution; these are
separate choices. A runtime configuration path is required. Relative paths are
resolved against the current working directory. The optional bridge assembly path
is reserved for the later managed-loading step.

Windows uses wide command-line arguments and native wide paths internally.
Unix uses native narrow paths. The discovered path is printed as UTF-8.
Buffer sizes are measured in hosting character units, including the null terminator,
and the implementation queries the required size rather than assuming a path limit.

## Loading behavior

The private `HostFxrLibrary` implementation opens the discovered absolute path with
`LoadLibraryW` on Windows or `dlopen` with `RTLD_NOW | RTLD_LOCAL` on Unix. Unix
link dependencies use CMake's `CMAKE_DL_LIBS` rather than a hardcoded library name.
It resolves:

- `hostfxr_initialize_for_runtime_config`
- `hostfxr_get_runtime_delegate`
- `hostfxr_close`

The function table is published only when all three exports exist. OS loading
failures include the path and platform diagnostic; symbol failures identify the
missing export. On Windows both include the captured native error number.
`InitializationResult.hostFxrLoaded` and `requiredExportsResolved` describe progress
of that attempt; neither flag means CoreCLR or the bridge has initialized.

Loading is serialized through a process cache. Repeated requests for the same file
reuse its module and function table. The first successfully opened hostfxr module
fixes the process's choice, even if its exports are incomplete. Requests to load a
different file are rejected; changing installations requires a process restart.

An OS opening failure is retryable. If opening succeeds but an export is missing,
the incomplete module is retained and its diagnostic cached until process exit.
Its incomplete function table is never published. Recovery requires restarting
the process with a usable installation. Successfully opened modules are never
passed to `FreeLibrary` or `dlclose`, including when export resolution fails.

## Context preparation

`Initialize` validates the runtime configuration path, loads the library, and calls
`hostfxr_initialize_for_runtime_config`. The returned opaque context owns temporary
hosting state; it is distinct from both the OS library handle and the runtime.
A private RAII guard closes every returned non-null context, including error paths.
The normal path explicitly checks `hostfxr_close`'s result before returning.

Statuses `0` (Success) and `1` (Success_HostAlreadyInitialized) are accepted when a
non-null context is returned. Status `2` (Success_DifferentRuntimeProperties) is a
successful hostfxr result, but this wrapper conservatively rejects it until property
compatibility can be verified. Rejection still closes the returned context.
Diagnostics identify the failing API and its hexadecimal status. Hostfxr's detailed
errors currently use its default stderr writer.

`runtimeConfigInitialized` reports accepted context preparation; `hostContextClosed`
reports successful explicit closure. Closing does not unload hostfxr or CoreCLR.
Calls on the same wrapper must be serialized by the caller. This step retains no
open context between calls, allowing repeated preparation attempts.

## Runtime delegate acquisition

Before closing an accepted context, `Initialize` calls `hostfxr_get_runtime_delegate`
with `hdt_load_assembly_and_get_function_pointer`. This starts CoreCLR if necessary,
or uses the already active compatible runtime. A zero return status and a non-null
pointer are both required. Failure diagnostics identify this API and its status;
a success status with a null pointer is also rejected.

The returned pointer is converted to the SDK's
`load_assembly_and_get_function_pointer_fn` type and retained in private wrapper
state after successful context closure. Its platform-specific ABI stays outside
the public header. The runtime owns the delegate; closing its host context does
not invalidate it. If delegate acquisition or closure fails, no new delegate is
published to wrapper state. Each new initialization attempt clears the previously
retained delegate, so a failed configuration cannot leave stale wrapper state.

`runtimeDelegateObtained` reports successful acquisition during this attempt. It
does not mean that a managed bridge method has been resolved. A false flag does
not prove CoreCLR is absent: another host may have started it, or runtime startup
may have partially progressed before an error. A successful delegate request is
an irreversible process-level step; there is no CLR unload when the wrapper dies.

The example exits with code one on preparation, delegate acquisition, or closure
failure. It exits with zero when all finish, while reporting bridge loading as
pending. The next step will use the retained delegate to resolve and call
`Bootstrap.GetBridgeVersion`; this step does not load the bridge or invoke it.

## Build the managed bridge

Build separately with the .NET 10 SDK:

```sh
dotnet build packages/FenrirScripting/Managed/Fenrir.ManagedHost/Fenrir.ManagedHost.csproj --configuration Debug
```

`Managed/Fenrir.ManagedHost/bin/Debug/net10.0` contains the bridge DLL, dependency
manifest, and generated runtime configuration. There are no external package
references. CMake may invoke `dotnet --list-sdks` for native pack discovery, but
does not build the managed bridge.

`Fenrir.ManagedHost.Bootstrap.GetBridgeVersion` is prepared for native invocation
with `UnmanagedCallersOnly` and an explicit Cdecl calling convention. It returns
bridge version `1`. The native library does not obtain or invoke it yet, and the
attribute does not make this ordinary managed DLL a native exported library.

## Lifetime contract

`DotNetHost` is neither copyable nor movable. Its constructor performs no discovery
or initialization. Its readiness query describes this wrapper's access to the bridge,
not whether another component has loaded CoreCLR into the process.

Loaded hostfxr modules have process lifetime. The wrapper retains a shared reference
to an immutable function table behind its private implementation; destroying it
releases only that reference and wrapper state, including its delegate pointer.
CoreCLR now starts during delegate acquisition and has process lifetime.
Closing a `hostfxr` context does not unload it.
Managed component unloading is a separate future responsibility.
Discovery never changes wrapper readiness or loads the discovered library.
Platform handles and hosting API types stay within the native implementation.

## References

- [Microsoft native hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting)
- [Microsoft hosting sample](https://github.com/dotnet/samples/tree/main/core/hosting)
- [nethost discovery contract](https://github.com/dotnet/runtime/blob/main/src/native/corehost/nethost/nethost.h)
- [hostfxr API declarations](https://github.com/dotnet/runtime/blob/main/src/native/corehost/hostfxr.h)
- [CoreCLR delegate signatures](https://github.com/dotnet/runtime/blob/main/src/native/corehost/coreclr_delegates.h)
- [Hosting status codes](https://github.com/dotnet/runtime/blob/main/src/native/corehost/error_codes.h)
- [dotnet SDK listing](https://learn.microsoft.com/en-us/dotnet/core/tools/dotnet#options-for-displaying-environment-information-and-available-commands)
- [Runtime identifiers](https://learn.microsoft.com/en-us/dotnet/core/rid-catalog)
- [Windows library loading](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryw)
- [Unix library loading](https://man7.org/linux/man-pages/man3/dlopen.3.html)
- [Unix symbol lookup](https://man7.org/linux/man-pages/man3/dlsym.3.html)
- [Native hosting API design](https://github.com/dotnet/runtime/blob/main/docs/design/features/native-hosting.md)
- [UnmanagedCallersOnly](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.unmanagedcallersonlyattribute)
- [GenerateRuntimeConfigurationFiles](https://learn.microsoft.com/en-us/dotnet/core/project-sdk/msbuild-props#generateruntimeconfigurationfiles)
- [Coral](https://github.com/StudioCherno/Coral/tree/main), an architectural reference for a broader C++/.NET wrapper
