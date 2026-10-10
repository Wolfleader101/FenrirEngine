# FenrirScripting implementation plan

## Mission

Build a general-purpose C++/.NET hosting library on CoreCLR, comparable in scope to Mono's embedding API and Coral, but designed for our own API usage. The core must be usable from **any** C++ project. FenrirEngine integration starts only after the hosting API is stable. See [ADR-0001](../../docs/adr/0001-own-dotnet-hosting-library-before-engine-integration.md).

Coral and Mono are architectural references, not specifications; copy nothing from them. Microsoft's hosting APIs and documentation are authoritative for behaviour.

Learning is part of the purpose. Explain decisions and lifetime contracts so the user can understand and assess the implementation.

## Roadmap

The work is tracked as tickets in `.scratch/dotnet-scripting/` (`spec.md` plus `issues/NN-*.md`). Each ticket lists what blocks it; work the frontier (tickets whose blockers are all done).

- **Phase A, review and prefactor (01–05):**
  - 01: review and scope decision (done; see `review-01.md`)
  - 02: bridge function table
  - 03: move demo calls out of the bridge
  - 04: cleanup
  - 05: standalone library boundary
- **Phase B, the API (06–16):**
  - 06: type handles
  - 07: method handles and typed invocation
  - 08: host exception and log sink
  - 09: managed objects and ownership
  - 10: objects as arguments
  - 11: fields and properties
  - 12: strings
  - 13: blittable structs
  - 14: arrays
  - 15: internal calls
  - 16: type enumeration and attributes
- **Phase C, lifetime and stabilisation (17–19):**
  - 17: reload and root-retention checks
  - 18: API stabilisation
  - 19: engine integration spec

## Scope and boundaries

- The hosting core depends on no engine package (ECS, rendering, editor, gameplay).
- With scripting disabled, an engine build never discovers .NET or invokes `dotnet`.
- Target Windows, Linux and macOS. Keep platform adapters out of public headers, and document which platforms were actually tested.
- Use the .NET 10 SDK host pack for the `net10.0` bridge. Discover hostfxr with `nethost`; no recursive installation scans, vendored SDK binaries or runtime submodules.
- Keep SDK types, OS handles and managed implementation details private.
- Exercise the real library through the example and tests; never build a second, separate host.
- Out of scope until a ticket says otherwise:
  - CI
  - thread safety and concurrent calls
  - async
  - generics
  - native-created delegates and events
  - debugger attach
  - file watching and shadow copying
  - gameplay integration

## Contracts to preserve

- **Host lifetime.** `DotNetHost` is non-copyable and non-movable. Construction does not start .NET. Each initialization attempt resets readiness. A failed managed call does not invalidate an initialized host.
- **Process lifetime.** CoreCLR and the loaded hosting libraries live for the whole process. Closing a hostfxr context or destroying `DotNetHost` does not unload CoreCLR. The bridge stays resident; components are collectible.
- **Ownership.** Public results own their storage. Borrowed inputs and outputs are only used for the duration the operation documents.
- **Handles.** Handles are opaque, validated on every use, and never reused. They are never CLR addresses, OS handles or raw `GCHandle` values. Owners may outlive the host and must release safely after it is gone.
- **Release versus collection.** Explicit release and observed collection are separate concepts. Library operations never force GC.
- **Wire signatures.** Signatures use explicit calling conventions and fixed-width types, and the native and managed declarations must agree. Ordinary managed exceptions are caught before returning across the boundary; native allocations keep normal C++ exception behaviour.
- **Threading.** Calls on a wrapper or owner must be externally serialized until a ticket deliberately changes this.

## Working method for every ticket

1. Read the ticket and the relevant code, then state the small outcome being implemented.
2. Explain the purpose, interface, ownership and failure behaviour. Make routine design choices yourself.
3. Implement only that outcome, test-first where a seam allows. Update the example and docs.
4. Run the real checks. Add reusable tests for ABI, ownership and unloading behaviour; don't add tests that just restate the implementation.
5. Report:
   - what changed, and in which files
   - the commands you ran and what you observed
   - official references
   - material limitations
6. Pause for oversight before starting the next ticket.

Ask for clarification only when a consequential decision can't be settled from the ticket, the ADRs and the code. Record hard-to-reverse decisions as ADRs in `docs/adr/`.

## Current state (after ticket 01)

Implemented and tested (6 CTests; Windows Debug re-verified in ticket 01):

- hostfxr discovery, loading and diagnostics
- the bridge handshake (ABI version 6)
- bounded managed diagnostics
- collectible component loading with private dependency resolution
- `int(int,int)` static invocation
- release, and observation of the unload (NotRequested, Pending or Collected)

Linux and macOS are untested. `review-01.md` lists the known problems and assigns each to a ticket.

## Build and run (Windows, PowerShell, from the repository root)

```powershell
dotnet build packages/FenrirScripting/Managed/Fenrir.ManagedHost/Fenrir.ManagedHost.csproj --configuration Debug
cmake -S packages/FenrirScripting -B out/build/scripting -DFENRIR_SCRIPTING_BUILD_TESTS=ON
cmake --build out/build/scripting --config Debug
ctest --test-dir out/build/scripting -C Debug --output-on-failure
```

The README has other layouts and the example's command line.

## Official references

- [Native hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting)
- [Microsoft hosting sample](https://github.com/dotnet/samples/tree/main/core/hosting)
- [UnmanagedCallersOnly](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.unmanagedcallersonlyattribute)
- [Understanding AssemblyLoadContext](https://learn.microsoft.com/en-us/dotnet/core/dependency-loading/understanding-assemblyloadcontext)
- [AssemblyDependencyResolver](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.loader.assemblydependencyresolver)
- [Assembly unloadability](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)
- [GCHandle](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.gchandle)
- [Bounded encoding with Encoder.Convert](https://learn.microsoft.com/en-us/dotnet/api/system.text.encoder.convert)
