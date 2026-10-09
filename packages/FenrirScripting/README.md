# FenrirScripting

Optional, independent C++/.NET hosting package. The native library has no dependency
on ECS, rendering, the editor, or other Fenrir modules.

The current step discovers and loads `hostfxr`, prepares a hosting context using
a `.runtimeconfig.json`, obtains the assembly-loading runtime delegate, and closes
the context. Obtaining the delegate starts or reuses CoreCLR. The native library
then loads the managed bridge, resolves `Bootstrap.GetBridgeVersion`, and calls it.
It also resolves `Bootstrap.TryAddInt32` for a small status-plus-output interop call
and `Bootstrap.CopyGreetingUtf8` for a caller-owned UTF-8 buffer. `Bootstrap.EchoUtf8`
decodes arbitrary native UTF-8 into a managed string and returns it through the same
buffer contract.
`IsInitialized` becomes true only when the returned bridge version matches `1`
and all four required entry points have resolved.

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
Bridge method resolved: true
Interop method resolved: true
Greeting method resolved: true
Echo method resolved: true
Bridge version: 1
Initialization succeeded: true
Diagnostic: Managed bridge initialized; bridge version 1.
Initialized after attempt: true
Add(20, 22): status=0, value=42
Add(INT32_MAX, 1): status=2, value=none
Call diagnostic: The managed addition overflowed a 32-bit integer.
Add after overflow: status=0, value=4
Echo ASCII: status=0, bytes match=true
Echo Unicode: status=0, bytes match=true
Echo empty: status=0, bytes match=true
Echo embedded null: status=0, bytes match=true
Echo malformed UTF-8: status=1, value=none
Echo after invalid input: status=0, bytes match=true
Echo bytes after wrapper destruction: true
Greeting after wrapper destruction: status=0
Greeting UTF-8 bytes: 27
Greeting: Hello from .NET — π 🐺
Greeting bytes match: true
```

The example accepts:

```text
FenrirHostingExample [--dotnet-root absolute-directory] runtimeconfig.json bridge.dll
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
still completes the bridge handshake with version `1`.

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

## Managed bridge handshake

After closing the context successfully, `Initialize` calls the retained assembly
loader with the absolute bridge path, assembly-qualified type
`Fenrir.ManagedHost.Bootstrap, Fenrir.ManagedHost`, and method name
`GetBridgeVersion`. The delegate-type argument is `UNMANAGEDCALLERSONLY_METHOD`
and the reserved argument is null. This obtains a native-callable pointer to the
attributed static method rather than the SDK's default component entry point.

The SDK assembly loader uses its own `CORECLR_DELEGATE_CALLTYPE` convention.
The bridge method instead uses Cdecl and takes no parameters, returning a 32-bit
integer. Its native function-pointer type explicitly matches the managed
`CallConvCdecl` declaration; platform details remain private.

A zero assembly-loader status and non-null method pointer are required before
calling the method. `bridgeMethodResolved` reports resolution, and the optional
`bridgeVersion` contains the returned value, even on a version mismatch. Version
`1` is the current contract. Any other value fails with expected/received diagnostics.
After a matching version, initialization also resolves `TryAddInt32` and
`CopyGreetingUtf8` and `EchoUtf8` using the same loader and UCO marker. `interopMethodResolved`
reports addition resolution; `greetingMethodResolved` reports greeting resolution.
`echoMethodResolved` reports echo resolution. Only after all four methods resolve
are their pointers published to wrapper state and
`success` and `IsInitialized` set to true. Each new attempt clears readiness first;
a failed attempt leaves it false even if a previous attempt succeeded.

The handshake returns a constant and must remain free of exceptions escaping the
managed method. Native C++ exception handling does not make that boundary safe.
The addition export uses the error-reporting contract described below.

The example exits with zero only when initialization and its interop checks succeed.
The deliberate overflow is an expected check. Configuration, hosting, method-resolution, and version failures exit with
one. A bridge failure can occur after CoreCLR and the assembly have loaded; resetting
wrapper state does not undo those process-resident resources. The bootstrap bridge
has process lifetime. Collectible gameplay components and unloading remain deferred.

## Integer interop and managed errors

`DotNetHost::AddInt32(left, right)` exercises the actual bridge using two signed
32-bit values. Its private native function pointer has the Cdecl signature
`int32_t(int32_t, int32_t, int32_t*)`, matching the C# `int` inputs, `int*` output,
and `int` status returned by `Bootstrap.TryAddInt32`. The method is static and
marked `UnmanagedCallersOnly` with `CallConvCdecl`; no automatic marshaling is involved.

The output pointer refers to writable native storage for this call only. Managed
code never retains it. A null pointer is rejected; any non-null pointer must satisfy
the caller's storage contract. The bridge clears the output before attempting
`checked(left + right)`, catches `OverflowException`, and catches other ordinary
managed exceptions. The catch bodies return constant statuses, without allocating
an error string. Fatal runtime failures and invalid native addresses are outside
this contract.

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
storage, even if managed code wrote to it. The C++ diagnostic owns its text and
currently describes the status rather than transferring managed exception details.
A failed addition leaves the host ready for another call. Reinitialization still
clears readiness and all method pointers before attempting a new configuration.
Calls must be serialized with initialization on the same wrapper.

Rebuild the managed bridge as well as the native example for this step. A previous
version-1 bridge without a required method fails method resolution cleanly. The bridge
project enables unsafe blocks for explicit output pointers. Object handles,
callbacks, and collectible component unloading remain future steps.

## UTF-8 strings and buffer ownership

`DotNetHost::GetGreeting()` returns `Utf8StringResult`: a status, an optional owning
`std::string` of UTF-8 bytes, and an owning diagnostic. It calls the private Cdecl
entry point `int32_t(uint8_t*, int32_t, int32_t*)`, matching
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
Managed diagnostic strings can adopt this buffer contract in a later step.

## Native input and managed string round trips

`DotNetHost::EchoUtf8(std::string_view input)` borrows UTF-8 bytes for the duration
of its two managed calls and returns the same `Utf8StringResult` as `GetGreeting`.
The caller must keep the input valid and unchanged throughout the query and copy.
The wrapper checks its size against `INT32_MAX` before narrowing to the bridge's
32-bit length; excessive lengths return `InvalidArgument` without a managed call.
An uninitialized wrapper returns `NotInitialized` first.

The private Cdecl entry point is
`int32_t(const uint8_t*, int32_t, uint8_t*, int32_t, int32_t*)`: input pointer,
input byte count, output pointer, output capacity, and required byte count.
`Bootstrap.EchoUtf8` uses the matching C# byte pointers and `int` fields.
A null input pointer is valid only with length zero. Negative lengths, negative
capacities, null count pointers, and null output with nonzero capacity are invalid.
All native storage must be valid for its declared length and the input, output,
and count storage must not overlap. Managed code never retains these pointers.

Inside its exception handling, the export constructs `UTF8Encoding(false, true)`
and calls `GetString` on a borrowed `ReadOnlySpan<byte>`. This creates a real
managed string and rejects malformed UTF-8 rather than replacing bad sequences.
`DecoderFallbackException` becomes `InvalidArgument` (`1`); other ordinary managed
exceptions become `ManagedException` (`3`). Valid text is re-encoded without
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
and managed bridge, because an older bridge without the echo export fails method
resolution cleanly.

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

`Fenrir.ManagedHost.Bootstrap.GetBridgeVersion` is invoked by the native host
with `UnmanagedCallersOnly` and an explicit Cdecl calling convention. It returns
bridge version `1`. The attribute does not make this ordinary managed DLL a native
exported library; the runtime delegate resolves the method.

## Lifetime contract

`DotNetHost` is neither copyable nor movable. Its constructor performs no discovery
or initialization. Its readiness query describes this wrapper's access to the bridge,
not whether another component has loaded CoreCLR into the process.

Loaded hostfxr modules have process lifetime. The wrapper retains a shared reference
to an immutable function table behind its private implementation; destroying it
releases only that reference and wrapper state, including its delegate and method pointers.
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
- [UTF-8 encoding](https://learn.microsoft.com/en-us/dotnet/api/system.text.utf8encoding)
- [Encoding byte counts](https://learn.microsoft.com/en-us/dotnet/api/system.text.encoding.getbytecount)
- [Encoding into a span](https://learn.microsoft.com/en-us/dotnet/api/system.text.encoding.getbytes)
- [Decoding bytes into a managed string](https://learn.microsoft.com/en-us/dotnet/api/system.text.encoding.getstring?view=net-10.0)
- [Strict UTF-8 encoding and decoding](https://learn.microsoft.com/en-us/dotnet/api/system.text.utf8encoding.-ctor)
- [GenerateRuntimeConfigurationFiles](https://learn.microsoft.com/en-us/dotnet/core/project-sdk/msbuild-props#generateruntimeconfigurationfiles)
- [Coral](https://github.com/StudioCherno/Coral/tree/main), an architectural reference for a broader C++/.NET wrapper
