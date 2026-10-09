# FenrirScripting implementation handoff

## Mission and authority

Implement an optional, platform-agnostic C++/.NET hosting library inside
FenrirEngine, using CoreCLR rather than Mono. The user oversees and reviews the
work; the agent is responsible for design, implementation, verification, and
documentation. Learning remains a purpose: explain the decisions and lifetime
contracts so the user can understand and assess the implementation.

The user authorizes implementation of this roadmap. Work through one small,
reviewable step at a time. Complete and verify that step, report the result, and
pause for oversight before beginning the next step. A milestone below may need
several steps. Do not implement the entire roadmap in one turn or ask the user
to write the implementation.

We are building our own internal library with a similar purpose to Coral. Coral
is an architectural reference, not a specification or implementation to copy.
Prefer Microsoft's hosting APIs and official documentation when determining
behavior.

## Scope and boundaries

- Keep hosting independent of ECS, rendering, the editor, and gameplay APIs.
- Exercise the real library through `Examples/Hosting`; avoid an example that
  implements a second, separate host.
- Keep the engine's scripting build optional. With scripting disabled, neither
  SDK discovery nor any `dotnet` command may be required or invoked.
- Target Windows, Linux, and macOS. Separate platform adapters from public APIs
  and document which platforms have actually been tested.
- Use the .NET 10 SDK host pack for the current `net10.0` bridge. Continue using
  `nethost` for discovery; do not replace it with a recursive installation scan,
  vendored SDK binaries, or a runtime submodule.
- Keep SDK types, OS module handles, and managed implementation details private.
- Defer gameplay integration until hosting, interop, ownership, and unloading
  pass the completion gates below.

Do not expand this effort into a general reflection framework, editor tooling,
arbitrary type marshalling, asynchronous execution, or a public string class
before an actual requirement justifies it.

## Inspect before changing anything

Paths below are relative to `packages/FenrirScripting` unless stated otherwise.

Read repository instructions, inspect Git status, and inspect the actual source.
There are existing modified and untracked implementation files. Preserve them
and unrelated changes; the handoff is not permission to reset, clean, stage,
commit, or push the workspace. Treat source as authoritative if it has changed
since this snapshot.

Start with:

- Root `CMakeLists.txt`, package `CMakeLists.txt`, and `README.md`.
- `cmake/Nethost.cmake` and `cmake/DiscoverDotNetNative.cmake`.
- `Native/DotNetHost/include/FenrirDotNetHost/DotNetHost.hpp`.
- `Native/DotNetHost/src/DotNetHost.cpp`, `HostFxrDiscovery.cpp`,
  `HostFxrLibrary.*`, `HostFxrDiagnostics.*`, `StringEncoding.*`, and
  `Utf8Buffer.hpp`.
- `Managed/Fenrir.ManagedHost/Fenrir.ManagedHost.csproj`, `Bootstrap.cs`, and
  `BridgeStatus.cs`.
- `Examples/Hosting/main.cpp` and its `CMakeLists.txt`.

## Current baseline, inspected 2026-10-09

The native library is `FenrirDotNetHost`, with example target
`FenrirHostingExample`. C++20 and CMake 3.20 or later are used. The engine root
defaults `FENRIR_ENABLE_SCRIPTING` to OFF; configuring this package directly
defaults it to ON. The managed class library is built separately with `dotnet`.

Implemented:

- Automatic discovery of a complete stable .NET 10 native host pack, with explicit
  build-time overrides and target RID selection.
- `nethost` discovery of `hostfxr`, OS-specific dynamic loading, required export
  resolution, and owning hostfxr diagnostics.
- Runtime configuration initialization, acquisition of
  `load_assembly_and_get_function_pointer`, and temporary host context cleanup.
- Bridge loading, version handshake (currently version 1), and required method
  resolution before publishing readiness.
- `AddInt32`: checked managed arithmetic, status plus optional native value,
  overflow containment, and recovery on the next call.
- `GetGreeting`: caller-owned UTF-8 output and an owning native string.
- `EchoUtf8`: strict native UTF-8 decoding into a real managed string and copying
  back, preserving empty input, Unicode, and embedded nulls.
- Private UTF-8/hosting-character conversion utilities.

The user reports the real Windows example succeeds: 20 + 22 is 42, overflow
returns status 2 without a value, malformed UTF-8 returns status 1, later calls
recover, and returned strings remain valid after wrapper destruction. Linux and
macOS execution has not yet been verified. Some earlier checks live in ignored
`out/` directories; these are temporary evidence, not a portable test suite.

Still missing: actual per-call managed exception details, separate collectible
components, managed object ownership, verified unloading, and reload support.
The last proposed next step was managed diagnostics for `AddInt32`; it has not
been implemented.

## Contracts to preserve

`HostConfiguration` names a runtime configuration, bridge assembly, and optional
.NET root. `DotNetHost` is non-copyable and non-movable. Construction does not
start .NET. Readiness belongs to the wrapper; each initialization attempt resets
it. Failed managed calls do not by themselves invalidate an initialized host.

CoreCLR and loaded native hosting libraries have process lifetime. Closing a
hostfxr context or destroying `DotNetHost` does not unload CoreCLR. The bridge
remains resident; component unloading will be a separate responsibility.

Public native results own their text. Borrowed input/output storage is retained
only for the duration documented by the operation. Wire signatures use explicit
calling conventions and fixed-width values; native and managed declarations
must agree. Ordinary managed exceptions must be caught before returning across
an unmanaged boundary. Native allocations retain normal C++ exception behavior.

Calls on a wrapper currently require external serialization. Preserve and
document that restriction until a later step deliberately replaces it.

## Working method for every step

1. Inspect the relevant code and state the small outcome being implemented.
2. Explain the purpose, concepts, intended interface, ownership, and failure
   behavior. Make routine design choices yourself within this plan.
3. Implement only that outcome and update its example and documentation.
4. Run meaningful checks against the real library. Add reusable tests for
   substantial ABI, ownership, and unloading behavior; avoid tests that merely
   repeat implementation details.
5. Report what changed, the files involved, exact build/run commands, observed
   results, official references, and material limitations.
6. Pause for oversight. Address the user's feedback before moving forward.

Ask for clarification only when a consequential scope or behavior decision
cannot be resolved from this plan and the code. Do not request permission again
for routine edits already authorized here.

## Milestone 1: Managed diagnostics, starting with addition

Purpose: a native caller should receive useful details from the actual managed
failure, rather than only a hardcoded description of its status.

Modify `Bootstrap.cs`, `DotNetHost.cpp`, the public result documentation/API as
needed, `Examples/Hosting/main.cpp`, and `README.md`. Introduce a private managed
diagnostic helper only when that responsibility warrants a separate file.

Keep the public `AddInt32` call simple. Supply bounded caller-owned UTF-8
diagnostic storage during the operation; return the written byte count and an
explicit indication of truncation. Capture exception details inside managed
exception handling. Preserve the operation's original status if diagnostics
cannot be produced, and provide a native fallback. Truncation must preserve
valid UTF-8. Avoid shared last-error state and avoid repeating an operation to
size its error message.

Version the bridge ABI when changing its raw signature, or use a distinct
versioned entry point. An incompatible bridge must fail initialization before
invoking an incompatible function pointer.

Done when: overflow still returns status 2 with no value, its diagnostic
identifies `OverflowException`, success has no stale error text, later calls
recover, and checks cover Unicode, truncation, ownership, and version mismatch.
Do not assert exact localized exception wording. Follow with a separate small
step applying the established contract to other exports where useful.

## Milestone 2: Load a separate managed component

Purpose: separate the permanent hosting bridge from code whose lifetime we want
to control.

Add an example class library, provisionally
`Examples/Hosting/ManagedComponent/Fenrir.HostingComponent.csproj`. Add internal
managed component/context responsibilities under `Managed/Fenrir.ManagedHost`;
keep `Bootstrap` focused on ABI entry points. Add a small native component API
under `Native/DotNetHost`, with file names chosen after inspecting the existing
design.

The resident bridge creates a collectible `AssemblyLoadContext` and loads the
component there. Resolve component dependencies with `AssemblyDependencyResolver`
and define which shared contracts remain in the default context. Do not load a
second copy of a shared contract assembly accidentally.

Native code receives an opaque component token, not a CLR object address or an
OS handle. Associate tokens with their owning session, validate them on every
call, reserve an invalid token, and prevent stale tokens from selecting a reused
entry. Decide cleanup ownership before adding a registry; wrapper destruction
must not silently orphan component state in a permanent bridge registry.

Done when: the example loads the component, reports its identity, rejects a
missing assembly with an owning diagnostic, and the original bridge calls still
work. Add a separate dependency fixture to verify dependency resolution in a
subsequent small step.

## Milestone 3: Invoke one component method

Purpose: prove that native requests can execute replaceable component code.

Modify the component fixture, managed registry/invocation implementation, native
component API, and example. Begin with one named static method with a constrained
integer signature; do not introduce arbitrary argument marshalling.

Route calls through resident bridge entry points. Keep component reflection
objects/delegates internal and account for their cleanup. Do not hand C++ an
unmanaged function pointer into a collectible component whose lifetime it cannot
validate.

Done when: a known component calculation succeeds, a missing method and thrown
exception produce useful failures, and the next valid invocation succeeds.

## Milestone 4: Establish unloading before adding objects

Purpose: prove the simplest collectible lifetime before introducing more roots.

Add an explicit component unload request and distinguish requested/pending from
observed completion. Invalidate access before releasing the component's managed
references and invocation caches. Track completion using a weak reference to
the context, without retaining the context through the tracking record.

Unload is cooperative. Calling `Unload()` initiates it; collection requires
executing calls and strong references to stop retaining the context. Use bounded
GC/finalizer cycles in verification code, with appropriate helper scope
boundaries. Do not force full GC on every normal library operation or promise a
synchronous unload deadline. See Microsoft's
[unloadability guide](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability).

Done when: load, invoke, request unload, and observe collection work repeatedly;
stale component calls are rejected; an intentional retained reference produces
a pending result, then completion after release; and the permanent bridge still
works throughout.

## Milestone 5: Managed objects and explicit ownership

Purpose: keep managed objects alive safely while native code uses them, then
release every root that would prevent component unloading.

Add one small instance type to the fixture and operations to create, call, and
release it. Introduce an opaque object token and native ownership wrapper when
its lifetime contract is clear. Use a validated managed registry or equivalent
rooting mechanism; never expose raw managed addresses or accept unchecked
`GCHandle` values as public tokens.

Record the owning component/session and enforce stale-token and cross-owner
checks. Start with a documented policy that explicit unload is rejected while
owned objects remain live. Native cleanup must have a safe release path even
when the original `DotNetHost` wrapper is gone; choose shared session ownership
or another explicit lifetime arrangement and test it. Session destruction must
release its owned objects and request component cleanup without claiming that
CoreCLR has stopped.

Done when: an object survives managed GC while owned, stateful calls work,
release removes its root, stale/double-release cases are handled as documented,
and a component becomes collectible after its final object is released. Verify
destruction order and failed construction paths.

## Milestone 6: Reload and lifetime failure checks

Purpose: demonstrate that the design supports replacement of component code,
rather than merely loading it once.

Add two fixture versions with observably different behavior. Load version A,
invoke it, release objects, observe unload, and load version B in a fresh
context. Keep component and object identities distinct across generations.
Begin with separate fixture paths; file-watching and shadow-copy deployment are
later decisions.

Done when: version B's result is observed, all old tokens remain invalid, repeated
cycles complete, partial failures leave no registry roots, and host/bridge
operations continue to work. Add focused root-retention checks for caches,
delegates, and event subscriptions if those features have been introduced.

## Milestone 7: Reproducible builds and platform verification

Purpose: make the demonstrated behavior usable beyond one developer machine.

Update CMake, deployment instructions, and reusable verification targets as
needed. Keep real smoke checks running throughout earlier steps; this milestone
consolidates the full supported-platform matrix.

Verify Windows, Linux, and macOS on available runners, reporting unavailable
coverage explicitly. Exercise enabled and disabled builds, target pack selection,
Unicode paths/text, native dependency deployment, and the component lifecycle.
Use clean build directories and supported RID/architecture combinations.

Done when: documented commands reproduce builds and lifecycle checks on tested
platforms, a deliberately unusable `dotnet` executable is never invoked when
scripting is OFF, and missing enabled-build prerequisites produce actionable
diagnostics. Document the runtime, bridge, component dependencies, and native
libraries required for deployment.

## Completion gate and later work

This hosting foundation is complete when startup failures are diagnosable,
interop contracts are checked, native results own their storage, managed objects
have explicit ownership, collectible components demonstrably unload, reload does
not revive stale identities, and disabled builds need no .NET discovery.

Only then propose a separate gameplay integration plan. Callbacks, broader
reflection, more data types, asynchronous/concurrent invocation, editor reload,
and ECS bindings require their own requirements and lifetime designs.

## Current build and run commands

From the repository root in PowerShell:

```powershell
dotnet build packages/FenrirScripting/Managed/Fenrir.ManagedHost/Fenrir.ManagedHost.csproj --configuration Debug
cmake -S packages/FenrirScripting -B out/build/scripting
cmake --build out/build/scripting --config Debug --target FenrirHostingExample

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$bridgeDir = 'packages/FenrirScripting/Managed/Fenrir.ManagedHost/bin/Debug/net10.0'
.\out\build\scripting\Examples\Hosting\Debug\FenrirHostingExample.exe "$bridgeDir/Fenrir.ManagedHost.runtimeconfig.json" "$bridgeDir/Fenrir.ManagedHost.dll" | Out-Host
```

The executable path above assumes a Windows multi-configuration generator.
Consult the README for other layouts. The console encoding setting belongs to
the invoking shell, not the hosting library. Extend these commands as component
fixtures and verification targets are added.

## Official references

- [Native hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting)
- [Microsoft hosting sample](https://github.com/dotnet/samples/tree/main/core/hosting)
- [UnmanagedCallersOnly](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.unmanagedcallersonlyattribute)
- [Exception.ToString](https://learn.microsoft.com/en-us/dotnet/api/system.exception.tostring)
- [Bounded encoding with Encoder.Convert](https://learn.microsoft.com/en-us/dotnet/api/system.text.encoder.convert)
- [Understanding AssemblyLoadContext](https://learn.microsoft.com/en-us/dotnet/core/dependency-loading/understanding-assemblyloadcontext)
- [AssemblyDependencyResolver](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.loader.assemblydependencyresolver)
- [Assembly unloading and troubleshooting retained roots](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)
- [GCHandle](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.gchandle)

First assignment for the new agent: inspect the baseline, implement only the
`AddInt32` managed-diagnostic step from milestone 1, verify it, and present the
result for oversight.
