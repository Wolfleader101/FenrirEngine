# FenrirScripting

Optional, independent C++/.NET hosting package. The native library has no dependency
on ECS, rendering, the editor, or other Fenrir modules.

The current step discovers and loads `hostfxr`, prepares a hosting context using
a `.runtimeconfig.json`, obtains the assembly-loading and function-pointer runtime delegates, and closes
the context. Obtaining the delegates starts or reuses CoreCLR. The native library
then loads the managed bridge into the default load context and calls its single
public entry point, `Bootstrap.GetBridgeFunctions`, which fills a versioned table of
function pointers: component loading, identity copying, integer invocation,
release and weak unload observation.
`IsInitialized` becomes true only when the bridge reports version `8` and the
expected table size, and all six table entries are non-null.

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

The example needs the bridge's runtime configuration, the bridge DLL and a published
component. The test build produces all three under its build directory (see
"Reusable diagnostic checks"). From the repository root in PowerShell, after a
Visual Studio build with tests enabled:

```powershell
$managed = 'out/build/scripting/Tests/managed/Debug'
.\out\build\scripting\Examples\Hosting\Debug\FenrirHostingExample.exe "$managed/checks/ManagedDiagnostics.runtimeconfig.json" "$managed/checks/Fenrir.ManagedHost.dll" "$managed/component/Fenrir.HostingComponent.dll"
```

Expected output and exit code zero:

```text
Initialize: ok
Managed bridge initialized; bridge version 8.
Loaded Fenrir.HostingComponent
Calculations.Add(20, 22) = 42
Calculations.Throw: status 3
System.InvalidOperationException: Component failure — π 🐺.
Release: status 0, unload Pending
Call after release: status 5
```

The example accepts:

```text
FenrirHostingExample [--dotnet-root absolute-directory] runtimeconfig.json bridge.dll component.dll
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
separate choices. Both paths are required and must name accessible regular files.
Relative paths are resolved against the current working directory. Invalid paths
are rejected before starting CoreCLR.

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
- `hostfxr_set_error_writer`

The function table is published only when all four exports exist. OS loading
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

`Initialize` validates both configured paths, loads the library, and calls
`hostfxr_initialize_for_runtime_config`. The returned opaque context owns temporary
hosting state; it is distinct from both the OS library handle and the runtime.
A private RAII guard closes every returned non-null context, including error paths.
The normal path explicitly checks `hostfxr_close`'s result before returning.

Statuses `0` (Success) and `1` (Success_HostAlreadyInitialized) are accepted when a
non-null context is returned. Status `2` (Success_DifferentRuntimeProperties) is a
successful hostfxr result, but this wrapper conservatively rejects it until property
compatibility can be verified. Rejection still closes the returned context.
Diagnostics identify the failing API and its hexadecimal status. Hostfxr's emitted
explanation is captured into the returned diagnostic as described below.

`runtimeConfigInitialized` reports accepted context preparation; `hostContextClosed`
reports successful explicit closure. Closing does not unload hostfxr or CoreCLR.
Calls on the same wrapper must be serialized by the caller. This step retains no
open context between calls, allowing repeated preparation attempts.

## Hosting diagnostics

After loading hostfxr, `Initialize` uses a private `HostFxrDiagnostics` RAII scope
around context preparation, delegate acquisition, context closure, and bridge
resolution. The scope installs `hostfxr_set_error_writer` on the current thread
and restores the previous writer on every exit, including exception unwinding.
The context guard is declared later, so its unwind cleanup runs while capture is
still active. Nested scopes restore their outer capture; separate threads keep
independent buffers. These private scopes must be destroyed on their creating
thread, in reverse construction order.

The callback immediately copies borrowed messages through the private
`StringEncoding` helper. On Windows, UTF-16 messages are converted to UTF-8;
Unix hosting messages are validated as UTF-8 and copied. Multiple messages
are joined with newlines and appended to the owning `InitializationResult.diagnostic`
alongside the native API/status explanation. Returned text survives both wrapper
destruction and subsequent calls.

No C++ exception escapes the callback. If conversion or allocation fails there,
capture records that some text is unavailable and preserves the native status.
Formatting the final C++ result has the same allocation behavior as other owning
string results in this library.

During the scope, messages are captured rather than forwarded to the replaced
writer or the default stderr writer. The previous behavior resumes afterward.
Only messages emitted through this thread's hostfxr writer are captured; this is
not a process-wide stderr redirect or a managed exception reporting contract.
Hostfxr propagates its writer to hostpolicy during applicable hosting calls.
Some method-resolution failures emit only a status, so their existing API/status
diagnostic remains useful even when no additional text is available.

To observe a JSON failure in PowerShell from the repository root:

```powershell
Set-Content -LiteralPath out/invalid.runtimeconfig.json -Value '{' -Encoding UTF8
$bridgeDir = 'packages/FenrirScripting/Managed/Fenrir.ManagedHost/bin/Debug/net10.0'
.\out\build\scripting\Examples\Hosting\Debug\FenrirHostingExample.exe out/invalid.runtimeconfig.json "$bridgeDir/Fenrir.ManagedHost.dll"
```

The example exits with one and prints a diagnostic containing status `0x80008093`
plus the JSON parsing explanation. Running again with the valid configuration
still completes the bridge handshake with version `6`.

## Runtime delegate acquisition

Before closing an accepted context, `Initialize` obtains two runtime delegates with
`hostfxr_get_runtime_delegate`: `hdt_load_assembly` and `hdt_get_function_pointer`
(both .NET 8+). This starts CoreCLR if necessary, or uses the already active
compatible runtime. Each requires a zero status and a non-null pointer. The runtime
owns the delegates; closing the host context does not invalidate them.

`runtimeDelegateObtained` reports that both were obtained during this attempt. A
false flag does not prove CoreCLR is absent: another host may have started it, or
startup may have partly progressed before an error. Starting the runtime is an
irreversible process-level step; there is no CLR unload when the wrapper dies.

## Managed bridge handshake

See [ADR-0002](../../docs/adr/0002-bridge-in-default-load-context-with-function-table.md).

1. `load_assembly` loads the bridge into the **default** load context
   (`bridgeAssemblyLoaded`). A process holds one bridge assembly: loading a
   different file with the same assembly name fails and is reported.
2. `get_function_pointer` resolves `Fenrir.ManagedHost.Bootstrap.GetBridgeFunctions`
   with `UNMANAGEDCALLERSONLY_METHOD`. This is the only public bridge entry point.
3. Native code calls it with its expected version (currently `8`), a table buffer
   and the table size. The bridge always reports its own version and table size
   (`bridgeVersion`), and fills the table only if both match.
4. A version mismatch, size mismatch or null table entry fails initialization with
   a diagnostic naming the problem. No table entry is called before this check.

Only then is the table published and `IsInitialized` true. Each new attempt clears
readiness first. Component owners keep a shared reference to the table they were
loaded with, so they work after the host is reset or destroyed.

The table is a sequence of Cdecl function pointers. Its field order, count and
signatures are declared twice: in the managed `BridgeFunctions` struct, where
assigning `&Method` type-checks each signature, and in the private native
`Detail::BridgeFunctions`. Size assertions on both sides catch a different slot
count, and the behaviour checks call every slot. Adding an export means adding a
field on both sides and bumping the version.

The example exits with zero only when initialization and its component checks succeed.
Configuration, hosting, handshake and version failures exit with one. A bridge failure can occur after CoreCLR and the
assembly have loaded; resetting wrapper state does not undo those process-resident
resources. The bridge has process lifetime. Separate collectible components can be
loaded and invoked; unload status observes collection separately from release.

## Managed call results and diagnostics

Every bridge export returns an `int` status and writes outputs only through
caller-owned pointers, borrowed for that call and never retained.

| Wire value | Meaning |
| --- | --- |
| `0` | Success |
| `1` | Invalid argument |
| `2` | Checked arithmetic overflow |
| `3` | Another ordinary managed exception was caught |
| `4` | Output buffer too small |
| `5` | Invalid or released token |

`BridgeStatus.cs` and the public native `ManagedCallStatus` enum mirror these wire
values. They are not hostfxr/HRESULT statuses. Unknown values become the
wrapper-only `UnexpectedStatus` (`-2`), with the raw value in the diagnostic.
Calling before initialization returns `NotInitialized` (`-1`) without entering
managed code. A malformed response becomes `InvalidResponse` (`-3`).

A result's `value` is present only on success; failure never exposes output storage.
Each call supplies a 4096-byte diagnostic buffer. Catch blocks copy
`Exception.ToString()` into it as complete UTF-8 scalars, with no BOM or
terminator; `diagnosticTruncated` reports a shortened diagnostic. The operation runs
once and is never repeated to size an error. The native diagnostic owns its text and
outlives later calls and the host.

If formatting fails, or the reported count, flag or UTF-8 is invalid, native code
uses a status-specific fallback and keeps the original status. Success with
nonzero diagnostic metadata becomes `InvalidResponse`. A failed call leaves the
host ready. Native allocation and platform conversion failures keep normal C++
exception behavior.

## Reusable diagnostic checks

Enable checks explicitly; default builds still build the managed bridge separately:

```powershell
cmake -S packages/FenrirScripting -B out/build/scripting -DFENRIR_SCRIPTING_BUILD_TESTS=ON
cmake --build out/build/scripting --config Debug
ctest --test-dir out/build/scripting -C Debug --output-on-failure
```

The check target builds the managed bridge, a managed contract executable and one
fixture bridge, and publishes the example component with its private dependency
under the CMake build directory. Each native bridge variant runs in its own process
because the bridge lives in the default load context; the fixture selects its
handshake response from `FENRIR_TEST_BRIDGE_MODE` (`legacy` version, `oversized`
table, `missing` entry). `notbridge` passes a non-bridge assembly. The nine checks cover actual
UCO pointer calls, every UTF-8 truncation boundary, buffer canaries, a throwing
exception formatter, the real bridge handshake, owning diagnostics, malformed bridge
responses, query/copy failures without retries or partial results, recovery,
and ABI version rejection. String fixture checks exercise the UTF-8 adapter through `GetInfo` and
count calls to ensure failed queries never copy and failed copies never retry.
Component checks cover private registration counts, collectible identity, token
reuse prevention, failed-load cleanup, missing component exports, Unicode paths,
move/release behavior, and ownership after host reset/destruction.
Invocation checks exercise the actual Cdecl export and public native owner:
exact overload selection, wrong/generic/private/instance/UCO methods, open/nonpublic
types, missing types/methods, component exceptions (including ArgumentException),
overflow, UTF-8 and pointer validation, bounded diagnostics, recovery, and stale
tokens. Native fixture checks reject invalid diagnostic metadata and unknown
statuses, and count failed invocations to verify they are never repeated to obtain
diagnostics. A bridge whose table lacks only the unload-state entry fails
initialization before publishing readiness.
Dependency checks use the real registry and loader with isolated temporary
deployments. They resolve a derived type to trigger loading, verify distinct private
dependency assemblies in two collectible contexts, and verify resident bridge and
framework identity. A redundant bridge DLL does not override sharing; a newer
bridge reference, different culture, or different public-key token is rejected
by the loader. Missing dependency resolution fails
on type use, explicit release removes its registration, and a complete deployment
works afterward. A separate `ManagedUnloadContract` process observes collection
after eight load/invoke/release cycles, including component and private dependency
assemblies. A retained `Type` keeps unloading pending; dropping it permits
collection. Forgetting a pending observer does not retain the assemblies. Native
checks cover state validation, moved/released observations, and destructor cleanup.
All nine checks have been executed in Debug on Windows. The isolated managed
unload check also passes in Release with optimized JIT lifetimes. Linux and macOS
execution is pending.
With scripting disabled, test configuration is also skipped before .NET discovery.

## UTF-8 output buffers

`ManagedComponent::GetInfo` copies the component name with a two-call adapter: a
size query (null destination, zero capacity), then a copy into native storage of
exactly that size. Each call has its own diagnostic buffer. A failed query never
copies, a failed copy is never retried, and a changed byte count becomes
`InvalidResponse`. Only success exposes the owning string. Ticket 12 replaces this
with general string returns.

## Private host-string encoding

`Native/DotNetHost/src/StringEncoding.hpp/.cpp` centralizes the hostfxr text boundary.
Its `HostString` and `HostStringView` aliases use the SDK's `char_t`: UTF-16
`wchar_t` on Windows and UTF-8 `char` on Unix. These aliases stay private; the
public API and managed buffer ABI continue to use UTF-8. This is not a general
wide-string abstraction or a managed allocation owner.

`Utf8ToHostString(std::string_view)` and `HostStringToUtf8(HostStringView)` borrow
explicit-length input during the call and return owning strings. They preserve
embedded nulls, BOM code points, and the original normalization form. Empty
input succeeds. No terminator or BOM is added to the payload. Results remain
valid after the input storage changes or is destroyed.

Windows uses `CP_UTF8` with `MB_ERR_INVALID_CHARS` or `WC_ERR_INVALID_CHARS`.
Both the sizing and conversion calls are checked. Input lengths above `INT_MAX`
are rejected before narrowing to the API's `int`; explicit positive lengths
avoid scanning for a null terminator. Unix performs a locale-independent UTF-8
validation pass before copying, following Unicode Table 3-7. It rejects overlong
sequences, bad continuation bytes, truncated sequences, encoded surrogate code
points, and values above U+10FFFF. Unicode noncharacters remain valid scalars.

Malformed encoding throws `std::invalid_argument`. Oversized Windows input throws
`std::length_error`; other Windows API failures throw `std::system_error` with
the captured native error code. An inconsistent sizing/conversion result throws
`std::runtime_error`. Allocations retain normal C++ exception behavior. Any caller
at a C ABI boundary must catch these exceptions; `HostFxrDiagnostics` already
catches them and records that some diagnostic text could not be captured.

Verification covered the real Windows helper, all Unicode scalar values,
all two-byte input combinations, embedded nulls, malformed UTF-8/UTF-16,
ownership, and the existing hostfxr diagnostic capture checks. The UTF-8-host
branch was also compiled and exercised on Windows with a `char_t = char` test
stub. This branch check does not replace execution on Linux or macOS.

The hosting example prints a managed exception containing non-ASCII text. Console display is a separate
boundary. In PowerShell, explicitly setting the output encoding also updates
the shell's cached encoding before it decodes native output:

```powershell
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
```

The encoding helper never changes console settings.

## Build the managed bridge

Build separately with the .NET 10 SDK:

```sh
dotnet build packages/FenrirScripting/Managed/Fenrir.ManagedHost/Fenrir.ManagedHost.csproj --configuration Debug
```

`Managed/Fenrir.ManagedHost/bin/Debug/net10.0` contains the bridge DLL, dependency
manifest, and generated runtime configuration. There are no external package
references. CMake may invoke `dotnet --list-sdks` for native pack discovery, but
does not build the managed bridge.

`Fenrir.ManagedHost.Bootstrap.GetBridgeFunctions` is invoked by the native host
with `UnmanagedCallersOnly` and an explicit Cdecl calling convention. It fills the
version-8 function table. The attribute does not make this ordinary managed DLL a
native exported library; the runtime delegate resolves the method.

## Lifetime contract

`DotNetHost` is neither copyable nor movable. Its constructor performs no discovery
or initialization. Its readiness query describes this wrapper's access to the bridge,
not whether another component has loaded CoreCLR into the process.

Loaded hostfxr modules have process lifetime. The wrapper retains a shared reference
to an immutable function table behind its private implementation; destroying it
releases only that reference and wrapper state, including its delegate and method pointers.
CoreCLR now starts during delegate acquisition and has process lifetime.
Closing a `hostfxr` context does not unload it.
Component ownership is separate from runtime lifetime, as described below.
Discovery never changes wrapper readiness or loads the discovered library.
Platform handles and hosting API types stay within the native implementation.

## Separate collectible components

Build the example component independently of the bridge. It targets `net10.0`
and uses the bridge's already initialized runtime; the host does not initialize
the component's generated runtime configuration. The component references the
separate `ManagedDependency/Fenrir.HostingDependency.csproj` and compiles against
the resident bridge. The bridge reference uses `Private=false` and
`ExcludeAssets=runtime` so a normal component build does not copy a private bridge.
It has no engine dependency. The test build publishes it with its dependency; see
"Build the native library and example" for running the example against it.

`LoadComponent(path)` returns a `ComponentLoadResult` containing an owning
`unique_ptr<ManagedComponent>` only on success. Relative paths become absolute
before crossing the UTF-8 managed interface. Missing and invalid assemblies
produce owning managed diagnostics; failure leaves host readiness intact. Native
allocations and path conversion retain normal C++ exception behavior.

The move-only component provides `IsLoaded()`, `GetInfo()`, `InvokeInt32()`,
`Release()` and `GetUnloadStatus()`.
Information is a native-owned snapshot of the assembly name and actual verified
collectibility. Both ownership and metadata can outlive `DotNetHost`. Component
owners retain immutable resident bridge functions independently of subsequent
host initialization attempts. Externally serialize calls, moves and destruction
on each owner. The internal registry is synchronized across separate owners.

The managed registry assigns nonzero uint64 tokens that are never reused.
Neither these tokens nor managed addresses appear in the public interface.
Stale token access returns `InvalidHandle` (`5`); native moved-from/released
owners also reject information queries and invocations. Native `Release()` is idempotent and
invalidates ownership. Managed removal precedes the unload request, even if that
request throws. Destruction and move-assignment of a live owner perform
best-effort release without throwing; use explicit release to observe errors.

Native ownership storage is allocated before registration. Failed managed loads
roll back their context; native validation/diagnostic failures after a returned
token also release it through the ownership guard. The bootstrap bridge cannot
be loaded as a collectible component. SDK types and platform handles remain private.

`ComponentLoadContext` uses `AssemblyDependencyResolver` for managed/native
dependency paths and normal runtime fallback for unresolved assemblies. A
reference to the resident bridge shares that existing assembly. The fixture has
a separate private dependency: resolving `ComponentMarker` resolves its base
type from `Fenrir.HostingDependency` in that same collectible context. Framework
assemblies remain shared. Sharing a resident bridge assembly preserves type
identity; the loader explicitly rejects a requested version newer than the
resident bridge, or a different culture/public-key token, before sharing it.
Bridge assembly versions and the native bridge ABI handshake are separate checks.

Deploy the component DLL, `Fenrir.HostingComponent.deps.json`, and its private
dependency DLL together. `EnableDynamicLoading` copies private dependencies to
the normal build output. To prepare a deployment directory, use:

```powershell
dotnet publish packages/FenrirScripting/Examples/Hosting/ManagedComponent/Fenrir.HostingComponent.csproj --configuration Debug --output out/components/hosting
```

`AssemblyDependencyResolver` uses the component's dependency manifest to find
managed and native libraries. Loading the primary DLL does not eagerly validate
every dependency or type. A missing private dependency can fail later when a
type is resolved or code executes. The owner must still release that component;
a dependency failure does not automatically invalidate its registration. The
invocation export contains these failures and transfers owning diagnostic text;
other methods whose dependencies remain available can still be invoked.

## Constrained component invocation

```cpp
auto result = component.InvokeInt32("Fenrir.HostingComponent.Calculations", "Add", 20, 22);
```

`ManagedComponent::InvokeInt32(typeName, methodName, left, right)` returns
`Int32CallResult`. Names are case-sensitive UTF-8, borrowed only during this one
call and never retained. They must remain valid/unchanged throughout the call,
contain 1..INT32_MAX bytes, and contain no null characters. Native code checks
lengths/nulls before narrowing; managed code performs strict UTF-8 decoding.
Malformed names return `InvalidArgument` (`1`). A moved/released native owner
returns `InvalidHandle` (`5`) before argument validation or a managed call.

The resident bridge validates the nonzero component token and snapshots its
assembly under the registry lock. It releases that lock before type resolution
or user code runs. The named type must be public, closed, and defined in that
assembly. The method must be declared on it, public, static, non-generic, and
have exactly two by-value `System.Int32` parameters and a `System.Int32` return.
Inherited methods, varargs and `UnmanagedCallersOnly` methods are excluded.
Lookup does not use numeric conversions or select an incompatible overload.

`ComponentInvocation` resolves reflection metadata and creates a temporary
`Func<int,int,int>` delegate, then calls it as managed code. C++ receives no
function pointer into the collectible assembly. There is no resident cache of
component `Type`, `MethodInfo` or delegate objects. An accepted in-flight call
retains its assembly/delegate while executing; cooperative unloading must wait
for those roots/call frames to disappear. Calls, moves, release and destruction
on the same native owner still require external serialization.

The private Cdecl export takes, in order: uint64 token, type byte pointer/int32
length, method byte pointer/int32 length, two int32 inputs, int32 output pointer,
diagnostic byte pointer/int32 capacity, int32 written pointer, and int32 truncation
pointer. All pointers are borrowed; caller storage must be valid and non-overlapping.
Output and available diagnostic metadata are cleared before validation. Null
diagnostic storage is allowed only with zero capacity; value and both metadata
pointers are required. The output is published only after successful execution.

Missing types/methods and wrong signatures return `ManagedException` (`3`) with
exception details. Ordinary component exceptions also return `3`, including an
`ArgumentException` thrown by component code; it is distinct from malformed ABI
arguments. An `OverflowException` from invocation returns `Overflow` (`2`). A
stale raw token returns `5`. Typed delegate invocation keeps the original exception
instead of adding `TargetInvocationException`. Calls use the existing 4096-byte
owning diagnostic contract, preserving UTF-8 boundaries, truncation, fallback and
success-metadata checks. Failure exposes no integer and never retries execution.
Results and diagnostics remain valid after release and host destruction.

The fixture's `Calculations.Add` executes `DependencyMath.Add` in the private
dependency. The example checks 42, a thrown exception, invocation after host
destruction, and rejection after release. Rebuild both the bridge and native
example for ABI version 8.

## Observing cooperative unloading

`Release()` invalidates component access, clears the registry's strong references,
and requests cooperative unloading. `GetUnloadStatus()` returns a
`ComponentUnloadResult` with a status, optional `ComponentUnloadState`, and owning
diagnostics. The states are:

| State | Observation |
| --- | --- |
| `NotRequested` | The registration still owns its component. |
| `Pending` | Ownership was released; the context's long weak reference remains alive. |
| `Collected` | The released context's long weak reference is no longer alive. |

The same owner retains its private weak tracking token after release. It may be
moved, queried repeatedly, and queried after host destruction. A moved-from owner
returns `InvalidHandle` without a managed call. Native code rejects unknown states,
states inconsistent with its active/released lifecycle, and malformed success
diagnostic metadata without exposing a value. Release remains idempotent.

The managed record holds the component strongly only while active. After release
it holds only a long weak reference (`trackResurrection: true`) to the context.
Queries read `IsAlive` without retrieving the target or creating a strong root.
Destruction/move-assignment of an owner forgets its weak record after best-effort
release; forgetting a live registration is rejected. Forgetting an observation
does not cancel unloading. Raw stale/forgotten tokens return `InvalidHandle` (`5`).

The private Cdecl query takes uint64 token, int32 state output, and the usual
diagnostic byte pointer/int32 capacity/int32 written/int32 truncation pointers.
Failure leaves state `-1`. Forgetting takes the token and diagnostic arguments.
All pointers are borrowed, valid and non-overlapping; null diagnostic storage
requires zero capacity. Query output and both metadata pointers are required.

Neither release, query nor cleanup forces GC or promises a collection deadline.
`Pending` is expected immediately after release; the example also accepts
`Collected` if normal GC has already completed. An executing call, component
instance, reflected `Type`, delegate or other strong reference can delay collection.
CoreCLR and the bootstrap bridge remain resident throughout.

`ManagedUnloadContract` alone forces bounded GC/finalizer cycles as verification.
Its non-inlined load/invoke/release helper keeps assembly, context, delegate and
exception locals off the GC-loop frame. It observes long weak references to the
context, component assembly and private dependency, checks retained-reference
behavior, removes tracking records, rejects stale tokens, and invokes the resident
bridge afterward. The check is isolated with a 30-second CTest timeout. Managed
object ownership and reload are the next lifetime steps.

## References

- [Microsoft native hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting)
- [Microsoft hosting sample](https://github.com/dotnet/samples/tree/main/core/hosting)
- [Collectible contexts and cooperative unloading](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)
- [Weak references and resurrection tracking](https://learn.microsoft.com/en-us/dotnet/api/system.weakreference?view=net-10.0)
- [Loading an assembly by absolute path](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.loader.assemblyloadcontext.loadfromassemblypath)
- [Component dependency resolution](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.loader.assemblydependencyresolver)
- [Creating typed managed delegates](https://learn.microsoft.com/en-us/dotnet/api/system.reflection.methodinfo.createdelegate)
- [Reflection lookup flags](https://learn.microsoft.com/en-us/dotnet/api/system.reflection.bindingflags)
- [nethost discovery contract](https://github.com/dotnet/runtime/blob/main/src/native/corehost/nethost/nethost.h)
- [hostfxr API declarations](https://github.com/dotnet/runtime/blob/main/src/native/corehost/hostfxr.h)
- [Windows UTF-8 conversion](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte)
- [Windows UTF-8 to UTF-16 conversion](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar)
- [Unicode encoding forms and well-formed UTF-8](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/)
- [Console encoding and cached values](https://learn.microsoft.com/en-us/dotnet/api/system.console.outputencoding#remarks)
- [CoreCLR delegate signatures](https://github.com/dotnet/runtime/blob/main/src/native/corehost/coreclr_delegates.h)
- [Hosting status codes](https://github.com/dotnet/runtime/blob/main/src/native/corehost/error_codes.h)
- [dotnet SDK listing](https://learn.microsoft.com/en-us/dotnet/core/tools/dotnet#options-for-displaying-environment-information-and-available-commands)
- [Runtime identifiers](https://learn.microsoft.com/en-us/dotnet/core/rid-catalog)
- [Windows library loading](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryw)
- [Unix library loading](https://man7.org/linux/man-pages/man3/dlopen.3.html)
- [Unix symbol lookup](https://man7.org/linux/man-pages/man3/dlsym.3.html)
- [Native hosting API design](https://github.com/dotnet/runtime/blob/main/docs/design/features/native-hosting.md)
- [UnmanagedCallersOnly](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.unmanagedcallersonlyattribute)
- [Checked arithmetic and overflow](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/statements/checked-and-unchecked)
- [Exception details](https://learn.microsoft.com/en-us/dotnet/api/system.exception.tostring)
- [Decoder fallback failures](https://learn.microsoft.com/en-us/dotnet/api/system.text.decoderfallbackexception)
- [Unicode scalars and UTF-8 encoding](https://learn.microsoft.com/en-us/dotnet/api/system.text.rune)
- [UTF-8 encoding](https://learn.microsoft.com/en-us/dotnet/api/system.text.utf8encoding)
- [Encoding byte counts](https://learn.microsoft.com/en-us/dotnet/api/system.text.encoding.getbytecount)
- [Encoding into a span](https://learn.microsoft.com/en-us/dotnet/api/system.text.encoding.getbytes)
- [Decoding bytes into a managed string](https://learn.microsoft.com/en-us/dotnet/api/system.text.encoding.getstring?view=net-10.0)
- [Strict UTF-8 encoding and decoding](https://learn.microsoft.com/en-us/dotnet/api/system.text.utf8encoding.-ctor)
- [GenerateRuntimeConfigurationFiles](https://learn.microsoft.com/en-us/dotnet/core/project-sdk/msbuild-props#generateruntimeconfigurationfiles)
- [Coral](https://github.com/StudioCherno/Coral/tree/main), an architectural reference for a broader C++/.NET wrapper
