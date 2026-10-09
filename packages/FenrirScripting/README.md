# FenrirScripting

Optional, independent C++/.NET hosting package. The native library has no dependency
on ECS, rendering, the editor, or other Fenrir modules.

The current step locates `hostfxr` through `nethost`. Discovery returns an owning
path or a diagnostic containing the native failure code. It does not load `hostfxr`
or start CoreCLR. `Initialize` still deliberately returns a not-implemented failure,
and `IsInitialized` remains false. Managed invocation and unloading come later.

## Select the target hosting SDK files

Enabled native builds require the SDK host pack for the target platform and
architecture. Set `FENRIR_DOTNET_NATIVE_DIR` to its native directory:

```text
<dotnet-root>/packs/Microsoft.NETCore.App.Host.<rid>/<version>/runtimes/<rid>/native
```

For example, this machine has the Windows x64 pack at:

```text
C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Host.win-x64/10.0.12/runtimes/win-x64/native
```

Use the corresponding target RID on other systems, such as `linux-x64` or
`osx-arm64`. Cross-compilation requires the target's pack, not the build machine's
pack. Selection is explicit; CMake does not guess SDK versions or run `dotnet`.

Every directory must contain `nethost.h` and the shared native dependency:

| Target | Required library files |
| --- | --- |
| Windows | `nethost.dll` and its import library `nethost.lib` |
| Linux | `libnethost.so` |
| macOS | `libnethost.dylib` |

The header and native loader details remain private to the implementation.

## Build the native library and example

From the repository root, with a C++20 compiler and CMake 3.20 or later, replace
`<native-directory>` with the absolute SDK native directory selected above:

```sh
cmake -S packages/FenrirScripting -B out/build/scripting -DFENRIR_DOTNET_NATIVE_DIR="<native-directory>"
cmake --build out/build/scripting --config Debug --target FenrirHostingExample
```

The package defaults to enabled when configured directly. In the engine root build,
`FENRIR_ENABLE_SCRIPTING` defaults to `OFF`; enable it with
`-DFENRIR_ENABLE_SCRIPTING=ON` and supply the native directory. Set
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

With a discoverable installation, expected output and exit code zero (the path
depends on the installed runtime and platform):

```text
Initialized before discovery: false
Hostfxr discovered: true
Initialized after discovery: false
Hostfxr path: <absolute path to hostfxr>
Initialization succeeded: false
Diagnostic: CoreCLR hosting is not implemented yet.
Initialized after attempt: false
```

The example accepts:

```text
FenrirHostingExample [--dotnet-root absolute-directory] [runtimeconfig.json bridge.dll]
```

An omitted .NET root uses `nethost`'s environment/registered-installation search.
An explicit absolute root overrides it. A relative root is rejected by the wrapper.
For example, an absolute nonexistent root should print `Hostfxr discovered: false`,
an owning diagnostic with a hexadecimal native status, and exit with code one.
Discovery failure leaves the wrapper uninitialized.

`FENRIR_DOTNET_NATIVE_DIR` selects build-time files. `--dotnet-root` selects the
runtime installation to search; these are separate choices. Runtime configuration
and bridge assembly paths are passed to `Initialize` but are not inspected yet.

Windows uses wide command-line arguments and native wide paths internally.
Unix uses native narrow paths. The discovered path is printed as UTF-8.
Buffer sizes are measured in hosting character units, including the null terminator,
and the implementation queries the required size rather than assuming a path limit.

## Build the managed bridge

Build separately with the .NET 10 SDK:

```sh
dotnet build packages/FenrirScripting/Managed/Fenrir.ManagedHost/Fenrir.ManagedHost.csproj --configuration Debug
```

`Managed/Fenrir.ManagedHost/bin/Debug/net10.0` contains the bridge DLL, dependency
manifest, and generated runtime configuration. There are no external package
references. CMake does not invoke `dotnet` in this step.

`Fenrir.ManagedHost.Bootstrap.GetBridgeVersion` is prepared for native invocation
with `UnmanagedCallersOnly` and an explicit Cdecl calling convention. It returns
bridge version `1`. The native library does not obtain or invoke it yet, and the
attribute does not make this ordinary managed DLL a native exported library.

## Lifetime contract

`DotNetHost` is neither copyable nor movable. Its constructor performs no discovery
or initialization. Its readiness query describes this wrapper's access to the bridge,
not whether another component has loaded CoreCLR into the process.

Once hosting is implemented, CoreCLR and its native hosting libraries remain loaded
for the process lifetime. Wrapper destruction and closing a `hostfxr` context do not
unload CoreCLR. Managed component unloading is a separate future responsibility.
Discovery never changes wrapper readiness or loads the discovered library.
Platform handles and hosting API types stay within the native implementation.

## References

- [Microsoft native hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting)
- [Microsoft hosting sample](https://github.com/dotnet/samples/tree/main/core/hosting)
- [nethost discovery contract](https://github.com/dotnet/runtime/blob/main/src/native/corehost/nethost/nethost.h)
- [Native hosting API design](https://github.com/dotnet/runtime/blob/main/docs/design/features/native-hosting.md)
- [UnmanagedCallersOnly](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.unmanagedcallersonlyattribute)
- [GenerateRuntimeConfigurationFiles](https://learn.microsoft.com/en-us/dotnet/core/project-sdk/msbuild-props#generateruntimeconfigurationfiles)
- [Coral](https://github.com/StudioCherno/Coral/tree/main), an architectural reference for a broader C++/.NET wrapper
