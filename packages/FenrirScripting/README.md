# FenrirScripting

Optional, independent C++/.NET hosting package. The native library has no dependency
on ECS, rendering, the editor, or other Fenrir modules.

The current step discovers and loads `hostfxr`, prepares a hosting context using
a `.runtimeconfig.json`, obtains the assembly-loading and function-pointer runtime delegates, and closes
the context. Obtaining the delegates starts or reuses CoreCLR. The native library
then loads the managed bridge into the default load context and calls its single
public entry point, `Bootstrap.GetBridgeFunctions`, which fills a versioned table of
function pointers. The table holds the demo interop calls (`TryAddInt32`,
`CopyGreetingUtf8`, `EchoUtf8`) and the component operations: loading, identity
copying, integer invocation, release and weak unload observation.
`IsInitialized` becomes true only when the bridge reports version `7` and the
expected table size, and all nine table entries are non-null.

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
repository root, passing its generated runtime configuration and bridge DLL. For Windows PowerShell
with a Visual Studio build:

```powershell
.\out\build\scripting\Examples\Hosting\Debug\FenrirHostingExample.exe packages/FenrirScripting/Managed/Fenrir.ManagedHost/bin/Debug/net10.0/Fenrir.ManagedHost.runtimeconfig.json packages/FenrirScripting/Managed/Fenrir.ManagedHost/bin/Debug/net10.0/Fenrir.ManagedHost.dll
```

Expected output and exit code zero (the path depends on the installed runtime and platform):

```text
Initialized before discovery: false
Add before initialization: status=-1, value=none
Call diagnostic: The managed bridge is not initialized.
Greeting before initialization: status=-1
Echo before initialization: status=-1
Hostfxr discovered: true
Initialized after discovery: false
Hostfxr path: <absolute path to hostfxr>
Hostfxr loaded: true
Required exports resolved: true
Runtime configuration initialized: true
Runtime delegate obtained: true
Host context closed: true
Bridge assembly loaded: true
Bridge version: 7
Initialization succeeded: true
Diagnostic: Managed bridge initialized; bridge version 7.
Initialized after attempt: true
Add(20, 22): status=0, value=42
Add(INT32_MAX, 1): status=2, value=none
Call diagnostic: System.OverflowException: Arithmetic operation resulted in an overflow.
... managed stack trace (wording and paths vary) ...
Diagnostic truncated: false
Add after overflow: status=0, value=4
Echo ASCII: status=0, bytes match=true
Echo Unicode: status=0, bytes match=true
Echo empty: status=0, bytes match=true
Echo embedded null: status=0, bytes match=true
Echo malformed UTF-8: status=1, value=none
Echo diagnostic: System.Text.DecoderFallbackException: ...
... managed stack trace (wording and paths vary) ...
Echo diagnostic truncated: false
Echo after invalid input: status=0, bytes match=true
Diagnostic after wrapper destruction: true
Echo diagnostic after wrapper destruction: true
Echo bytes after wrapper destruction: true
Greeting after wrapper destruction: status=0
Greeting UTF-8 bytes: 27
Greeting: Hello from .NET — π 🐺
Greeting bytes match: true
```

The example accepts:

```text
FenrirHostingExample [--dotnet-root absolute-directory] runtimeconfig.json bridge.dll [component.dll]
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
3. Native code calls it with its expected version (currently `7`), a table buffer
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

The example exits with zero only when initialization and its interop checks succeed.
The deliberate overflow is an expected check. Configuration, hosting, handshake and
version failures exit with one. A bridge failure can occur after CoreCLR and the
assembly have loaded; resetting wrapper state does not undo those process-resident
resources. The bridge has process lifetime. Separate collectible components can be
loaded and invoked; unload status observes collection separately from release.

## Integer interop and managed errors

`DotNetHost::AddInt32(left, right)` exercises the actual bridge using two signed
32-bit values. Its private native function pointer has the Cdecl signature
`int32_t(int32_t, int32_t, int32_t*, uint8_t*, int32_t, int32_t*, int32_t*)`,
matching the C# inputs, result, diagnostic buffer, capacity, bytes written, truncation flag,
and `int` status returned by `Bootstrap.TryAddInt32`. The method is static and
marked `UnmanagedCallersOnly` with `CallConvCdecl`; no automatic marshaling is involved.

The output pointer refers to writable native storage for this call only. Managed
code never retains it. A null pointer is rejected; any non-null pointer must satisfy
the caller's storage contract. The bridge clears the output before attempting
`checked(left + right)`, catches `OverflowException`, and catches other ordinary
managed exceptions. Catch bodies copy `Exception.ToString()` into the supplied
diagnostic buffer under separate exception handling. Fatal runtime failures and
invalid native addresses are outside this contract.

| Wire value | Meaning |
| --- | --- |
| `0` | Success; the output contains the sum |
| `1` | Invalid argument, including a null output pointer |
| `2` | Checked arithmetic overflow |
| `3` | Another ordinary managed exception was caught |

`BridgeStatus.cs` and the public native `ManagedCallStatus` enum mirror these wire
values. They are not hostfxr/HRESULT statuses: positive values indicate failure,
and only zero means success. The addition wrapper accepts only these four values.
Unknown values become the wrapper-only `UnexpectedStatus` (`-2`), with the raw
value in the diagnostic. Calling before initialization returns the wrapper-only
`NotInitialized` (`-1`) without entering managed code.

`Int32CallResult.value` is present only on success. Failure never exposes output
storage, even if managed code wrote to it. The C++ diagnostic owns its UTF-8 text
and remains valid after later calls and wrapper destruction. The operation runs
once, with a 4096-byte diagnostic buffer; it is never repeated to size an error.
Managed code writes complete Unicode scalars, without a BOM or terminator.
Malformed UTF-16 in diagnostic text is replaced with U+FFFD. Byte counts preserve
embedded nulls. `diagnosticTruncated` reports a shortened managed diagnostic.

All pointers refer to valid, non-overlapping storage and are never retained.
The result, byte-count, and truncation pointers are required. Diagnostic storage
may be null only with zero capacity. Negative capacity is invalid. Available
outputs are cleared before validation; success writes zero diagnostic metadata.
The wire truncation flag is a 32-bit integer, either 0 or 1, rather than an ABI bool.

If formatting fails, the byte count and flag are zero and native code uses its
status-specific fallback. Invalid counts, flags, or UTF-8 also produce a fallback
with an explanation, preserving the original failure status. Success with
nonzero diagnostic metadata becomes `InvalidResponse` (`-3`) with no value.
Unknown operation statuses still become `UnexpectedStatus`. Native allocation
and platform conversion failures retain normal C++ exception behavior.
A failed addition leaves the host ready for another call. Reinitialization still
clears readiness and all method pointers before attempting a new configuration.
Calls must be serialized with initialization on the same wrapper.

Rebuild the managed bridge as well as the native example for this step. A previous
version-1 through version-5 bridge is rejected during the handshake before interop resolution. The bridge
project enables unsafe blocks for explicit output pointers. Object handles,
and callbacks remain future steps.

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
exception formatter, real native calls, owning diagnostics, malformed bridge
responses, query/copy failures without retries or partial results, recovery,
and ABI version rejection. String fixture checks exercise both adapters and
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

## UTF-8 strings and buffer ownership

`DotNetHost::GetGreeting()` returns `Utf8StringResult`: a status, an optional owning
`std::string` of UTF-8 bytes, and an owning diagnostic. It calls the private Cdecl
entry point `int32_t(uint8_t*, int32_t, int32_t*, uint8_t*, int32_t, int32_t*, int32_t*)`, matching
`Bootstrap.CopyGreetingUtf8(byte* destination, int capacity, int* requiredBytes)`.
No managed string, pointer into managed memory, or allocator ownership crosses the ABI.

The payload is the fixed greeting `Hello from .NET — π 🐺`. Counts and capacities
are signed 32-bit byte quantities, excluding any BOM or null terminator. Its 27
UTF-8 bytes differ from its UTF-16 character count. The bridge uses
`Encoding.UTF8.GetByteCount` to size it and the span overload of `GetBytes` to copy
directly into borrowed native memory. The span covers only the payload, even if
the caller supplies additional capacity. Pointers are never retained.

| Arguments or condition | Result |
| --- | --- |
| Null destination and zero capacity, valid count pointer | `Success` (`0`); report required bytes without copying |
| Non-null destination with enough capacity | `Success` (`0`); write the complete payload and report its byte count |
| Non-null destination with insufficient capacity, including zero | `BufferTooSmall` (`4`); report required bytes and leave payload storage untouched |
| Null count pointer, negative capacity, or null destination with nonzero capacity | `InvalidArgument` (`1`); no payload write; clear count when its pointer is valid |
| Ordinary managed exception during encoding | `ManagedException` (`3`); clear count and discard any payload |

Both pointers must designate valid native storage for this call and must not
overlap. A non-null destination must cover the declared capacity; the count
pointer must be writable. These are caller obligations, not checks that can be
performed safely from a raw pointer. Insufficient capacity never produces a
truncated string. The exception boundary catches ordinary managed exceptions;
invalid memory addresses and fatal runtime failures remain outside the contract.

The C++ wrapper queries the count, rejects negative lengths, allocates a native
string, and copies into its storage. Because this greeting is immutable, the copy
must report the same count as the query; a mismatch returns the wrapper-only
`InvalidResponse` (`-3`). Managed errors and unexpected statuses expose no value.
Native allocation failures retain normal C++ exception behavior. There is no
retry loop for changing text in this step.

`Utf8StringResult.value` owns the copied bytes independently of the wrapper and
later calls. The example compares them against an explicit UTF-8 literal after
destroying its host wrapper. `Greeting bytes match: true` and exit code zero are
the completion checks; console rendering alone depends on terminal configuration.
The wrapper's destruction still leaves CoreCLR and the managed bridge resident.
Both UTF-8 exports take the same trailing diagnostic arguments as addition:
buffer, capacity, bytes written, and an int32 truncation flag. Each query and copy
has its own 4096-byte native diagnostic storage. All payload and diagnostic
storage, including metadata, must be valid and non-overlapping. The diagnostic
buffer may be null only with zero capacity; both metadata pointers are required.
Available payload counts and diagnostic metadata are cleared before validation.

The failing invocation supplies `Utf8StringResult.diagnostic` and
`diagnosticTruncated`. Encoding/decoding exceptions use the guarded managed
writer; invalid arguments and undersized payload buffers can use the native
fallback without exception text. Original failure statuses survive unavailable
or invalid diagnostics. Success requires empty diagnostic metadata; violations
become `InvalidResponse`, with no value. Payload byte counts are independent of
diagnostic counts and must still agree between successful query and copy.

Native `ManagedDiagnosticBuffer.hpp` centralizes the owning diagnostic transfer
and validation used by addition, greeting and echo. A query failure returns
immediately; a copy failure discards its payload. Diagnostics never require
another operation invocation. Successful calls return empty diagnostics and
clear truncation. Error text remains owned after wrapper destruction.

## Native input and managed string round trips

`DotNetHost::EchoUtf8(std::string_view input)` borrows UTF-8 bytes for the duration
of its two managed calls and returns the same `Utf8StringResult` as `GetGreeting`.
The caller must keep the input valid and unchanged throughout the query and copy.
The wrapper checks its size against `INT32_MAX` before narrowing to the bridge's
32-bit length; excessive lengths return `InvalidArgument` without a managed call.
An uninitialized wrapper returns `NotInitialized` first.

The private Cdecl entry point is
`int32_t(const uint8_t*, int32_t, uint8_t*, int32_t, int32_t*, uint8_t*, int32_t, int32_t*, int32_t*)`:
input pointer, input byte count, output pointer, output capacity, required byte
count, diagnostic pointer/capacity, bytes written, and truncation flag.
`Bootstrap.EchoUtf8` uses the matching C# byte pointers and `int` fields.
A null input pointer is valid only with length zero. Negative lengths, negative
capacities, null count pointers, and null output with nonzero capacity are invalid.
All native storage must be valid for its declared length and the input, output,
and count storage must not overlap. Managed code never retains these pointers.

Inside its exception handling, the export constructs `UTF8Encoding(false, true)`
and calls `GetString` on a borrowed `ReadOnlySpan<byte>`. This creates a real
managed string and rejects malformed UTF-8 rather than replacing bad sequences.
`DecoderFallbackException` becomes `InvalidArgument` (`1`) with its exception
details; other ordinary managed exceptions become `ManagedException` (`3`) with
guarded diagnostics. Valid text is re-encoded without
normalization, BOM insertion/removal, or null-termination. Embedded nulls are data.

Greeting and echo share the private managed `ValidateUtf8Output` and
`CopyUtf8Payload` methods. An undersized destination returns `BufferTooSmall` (`4`)
and writes no payload. Invalid arguments or malformed input clear the count when
its pointer is valid and leave payload storage untouched. The helper runs inside
each export's exception handling; encoding initialization is also inside that
handling. Invalid memory addresses and fatal runtime failures remain outside the
caller-storage contract.

Native `src/Utf8Buffer.hpp` shares query, allocation, status mapping, and copy-length
checks between the two operations. It rejects negative or inconsistent counts
with `InvalidResponse` and exposes no partial value after any failed call. It
assumes a stable payload and does not retry. Its returned string owns its bytes;
native allocation failures retain normal C++ exception behavior. `StringEncoding`
continues to serve hostfxr's platform text representation; echo uses UTF-8 directly
in its managed interface on every platform.

The hosting example checks ASCII, `π 🐺`, empty text, and `A\0B` byte-for-byte. It
then passes malformed UTF-8, expects status `1` with no value, and checks a valid
call afterward. The last successful echo remains valid after both the input
storage and wrapper are destroyed. A call failure leaves readiness intact; a new
initialization attempt clears all method pointers. Rebuild both the native example
and managed bridge, because older bridge ABIs are rejected before interop
method resolution.

Verification exercised the real managed export, additional malformed sequences,
BOM and normalization preservation, a larger payload, raw pointer/capacity rules,
buffer canaries, and failure/reinitialization recovery on Windows. Fixture checks
also exercised failed copies, unexpected statuses, negative counts, and count
mismatches for both adapters. Linux and macOS execution remains untested.

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

The hosting example's existing byte comparison and malformed-runtimeconfig
diagnostic remain observable completion checks. Console display is a separate
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
version-7 function table. The attribute does not make this ordinary managed DLL a
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
It has no engine dependency:

```powershell
dotnet build packages/FenrirScripting/Managed/Fenrir.ManagedHost/Fenrir.ManagedHost.csproj --configuration Debug
dotnet build packages/FenrirScripting/Examples/Hosting/ManagedComponent/Fenrir.HostingComponent.csproj --configuration Debug
cmake --build out/build/scripting --config Debug --target FenrirHostingExample

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$bridgeDir = 'packages/FenrirScripting/Managed/Fenrir.ManagedHost/bin/Debug/net10.0'
$componentDir = 'packages/FenrirScripting/Examples/Hosting/ManagedComponent/bin/Debug/net10.0'
.\out\build\scripting\Examples\Hosting\Debug\FenrirHostingExample.exe "$bridgeDir/Fenrir.ManagedHost.runtimeconfig.json" "$bridgeDir/Fenrir.ManagedHost.dll" "$componentDir/Fenrir.HostingComponent.dll" | Out-Host
```

The component argument is optional; omitting it runs the existing interop checks.
With it, the example additionally reports:

```text
Component load: status=0
Component assembly: Fenrir.HostingComponent
Component collectible: true
Component Add(20, 22): status=0, value=42
Component missing method: status=3, value=none
Component thrown exception: status=3, value=none
Component after failures: status=0, value=4
Component after host destruction: true
Component invocation after host destruction: status=0, value=42
Component release: status=0
Component still owned: false
Component unload state: Pending
Component invocation after release: status=5
Component diagnostic after release: true
```

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
dependency. The example checks 42, a missing method, a thrown exception, recovery
to 4, invocation after host destruction, and rejection after release. Rebuild both
the bridge and native example for ABI version 7.

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
