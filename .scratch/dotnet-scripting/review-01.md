# Review 01: FenrirScripting as of `ba80544`

Scope: the whole `packages/FenrirScripting` package (native host, managed bridge, component registry, fixtures, tests, CMake, README, plan). Reviewed against the original plan's contracts and the new goal in [ADR-0001](../../docs/adr/0001-own-dotnet-hosting-library-before-engine-integration.md): an engine-independent, Coral/Mono-style hosting library.

**Baseline:** a clean configure, build and `ctest -C Debug` on Windows (MSVC, .NET 10) passes **6/6**. Linux and macOS have never been run.

**Severity:** **High** means wrong behaviour, or a design choice that will block the planned API. **Medium** means a real risk or significant friction. **Low** means a smell or a minor bug. **Info** means context with no direct fix.

**Fix ticket:** the ticket expected to fix each finding (02–05), a later API ticket where the fix naturally belongs, or *new ticket needed*.

Line numbers refer to the reviewed commit.

---

## Verdict

The lifetime thinking underneath is mostly sound, and worth keeping (see "Keep" below). The problems are in the surface around it:

- The ABI does not scale.
- Demo exports are welded into the production bridge.
- The result and status plumbing is copied around by hand.
- The documentation is about 3× larger than the code it describes.

None of the bugs found corrupt memory or leak managed roots. The most consequential issue is architectural: which load context the bridge lives in (D1).

## Keep (do not "clean up" these away)

- **Owners outlive the host.** `ManagedComponent` captures an immutable `shared_ptr` to the bridge's function pointers, so release stays safe after the `DotNetHost` is reset or destroyed.
- **Tokens are safe.** They are never reused (a monotonic `checked(++lastToken)`), zero is reserved as invalid, and they are validated on every call.
- **Release and collection are separate.** Release drops the strong roots. Collection is observed through a long weak reference. Library code never forces GC.
- **Failed loads roll back.** `ComponentRegistry.Load` unloads the context whenever registration did not complete.
- **Diagnostics are bounded.** Managed diagnostics are cut at UTF-8 scalar boundaries, and an exception thrown by `ToString()` is contained. Natively, `ApplyTo` refuses success results that carry diagnostic metadata.
- **hostfxr is handled carefully.** Its error-writer capture is scoped per thread, its module is cached for the process and never unloaded, and discovery goes through nethost.
- **Unload checks are strict.** They are bounded, the helper scopes are `NoInlining`, they also run in Release, and they track the component and its private dependency separately.

---

## Design / architecture

### D1. The bridge lives in an isolated load context, not the default one. **High.** Fix ticket: 02.

`load_assembly_and_get_function_pointer` loads each assembly path into its own isolated `AssemblyLoadContext`; it never uses the default one. The tests already prove this: `components.cpp` initializes the real bridge and then an incomplete fixture bridge that is also named `Fenrir.ManagedHost`, in the same process, and both load.

As a result:

- Two `DotNetHost`s with different bridge paths get **separate bridges with separate registries**.
- The plan's statement that "bridge/framework assemblies remain shared" is only true because `ComponentLoadContext.Load` special-cases the bridge by name.
- The managed test executable runs the bridge in the **default** context. Its topology is therefore different from production.

For a Coral-style API this matters. Script assemblies will compile against a public scripting API assembly, and that assembly must have exactly one copy for internal-call fields, attributes and type identity to work.

**Action for 02:** while replacing per-export lookup, decide where the bridge lives and record it in an ADR. The options are:
- keep the isolated context (one bridge per path); or
- use `hdt_load_assembly` plus `hdt_get_function_pointer` (.NET 8+), which loads into the default context.

### D2. Every new export costs an ABI bump, a by-name lookup and a readiness flag. **High.** Fix ticket: 02.

- `Initialize` (`DotNetHost.cpp:107–335`) is roughly 230 lines.
- It contains five near-identical blocks that resolve, check and format one method each (`:233–296`), then a separate loop for the six component exports (`:298–321`).
- Method names are written out twice under `#if defined(_WIN32)` because `char_t` differs per platform (`:24–49`, `:298–306`).
- `InitializationResult` has a bool for each method (`DotNetHost.hpp:38–44`).
- The version number 6 is hard-coded in three places: the managed `GetBridgeVersion`, the native `ExpectedBridgeVersion` (`:51`), and the fixture.

That is about 8 touch points per export. It won't scale to around 50 exports.

### D3. Demo calls are part of the production bridge and public API. **High.** Fix ticket: 03.

- `AddInt32`, `GetGreeting` and `EchoUtf8` are permanent exports in the bridge.
- They are also public members of `DotNetHost`.
- They are required for readiness: `IsInitialized()` checks their pointers (`:337–341`).

Separately, `ManagedCallStatus::Overflow` exists only to serve the demo. `InvokeComponentInt32` maps *any* `OverflowException` thrown by component code to it (`ComponentExports.cs:145`). That turns a user exception type into a library status, which is wrong for a general API.

### D4. Only the bridge is shared with components. **Medium.** Fix ticket: *new ticket needed* (or fold into 05).

`ComponentLoadContext.Load` shares only the bridge assembly. Everything else either resolves privately through `AssemblyDependencyResolver` or falls back to the framework. There is no concept of a configurable set of **shared contract assemblies**: the public scripting API that scripts compile against, and engine API assemblies later.

This needs a deliberate design before internal calls (15) or attributes (16) depend on type identity across contexts. I suggest a small ticket between 05 and 06: *"Shared contract assemblies"*.

### D5. Components are loaded from their path, so the file stays locked. **Medium.** Fix ticket: 17 (note).

`LoadFromAssemblyPath` (`ComponentRegistry.cs:49`, `ComponentLoadContext.cs:26`) maps the file. On Windows, the file can't be overwritten while it is loaded. Ticket 17 uses separate fixture paths, so this doesn't block it. Any later replace-in-place reload will need stream or shadow-copy loading, which the spec currently lists as out of scope.

### D6. Component results are wrapped in a pointer twice. **Low.** Fix ticket: 04.

`ComponentLoadResult::value` is a `std::unique_ptr<ManagedComponent>` (`ManagedComponent.hpp:94`). But `ManagedComponent` is already a movable pimpl around a `unique_ptr<State>`. Returning it by value (an optional or result type) removes one heap allocation and one layer of null checks.

### D7. There is no session concept. **Info.** Fix ticket: 09.

The plan asked for tokens to be "associated with their owning session". In practice tokens are global to a bridge instance. Native code can't misuse them across sessions today, because tokens are private and each owner captures its own bridge. Objects (09) are where cross-owner checks become real, so define the policy there.

---

## Correctness / lifetime

### C1. `LoadComponent` throws on Windows paths that aren't valid UTF-16. **Low.** Fix ticket: 04.

`Detail::HostStringToUtf8(absolute.native())` (`DotNetHost.cpp:419`) throws `std::invalid_argument` when a path contains an unpaired surrogate. The documented contract only allows allocation failures to throw. The fix is to return `InvalidArgument` with a diagnostic.

### C2. Owner destructors call into managed code, including at process exit. **Low.** Fix ticket: 04 (document).

`ManagedComponent::State::~State` calls `release` and `forgetUnload` (`ManagedComponent.cpp:12–22`). If an owner has static storage duration, these calls happen during static destruction, when calling into CoreCLR isn't guaranteed to be safe. The fix is to document that owners must not outlive `main`, or to make the destructor detect process teardown.

### C3. Status codes depend on exception types. **Low.** Fix ticket: 04 (and 07).

`LoadComponent` maps any `ArgumentException` to `InvalidArgument` (`ComponentExports.cs:29`). An `ArgumentException` thrown by the runtime during loading would then look like a caller mistake. Statuses should come from the bridge's own validation, never from the type of exception that was caught. The Overflow mapping (D3) is the same smell.

### C4. Results disagree on their default status. **Low.** Fix ticket: 04.

`ComponentUnloadResult` defaults to `InvalidHandle` (`ManagedComponent.hpp:41`). Every other result defaults to `NotInitialized`.

### C5. Strings are read with a query-then-copy round trip. **Low.** Fix ticket: 03 / 12.

`ReadUtf8Buffer` runs managed code twice: once to get the size, once to copy. It relies on the payload staying the same between the two calls. That holds for a constant name, but it's the wrong pattern for general string returns (ticket 12 already requires no second round trip).

---

## Overcomplication / duplication

### O1. Five result structs with hand-written status mapping. **Medium.** Fix ticket: 04.

`Int32CallResult`, `Utf8StringResult`, `ComponentInfoResult`, `ComponentReleaseResult` and `ComponentUnloadResult` all repeat the same status, value, diagnostic and truncated fields.

Every call site also has its own `switch` that maps a status to fallback text:
- `AddInt32`
- `LoadComponent`
- `InvokeInt32`
- `GetUnloadStatus`
- `Release`
- `ReadUtf8Buffer`

`Release()` even uses a `Utf8StringResult` as a temporary just to carry a status (`ManagedComponent.cpp:142`). One shared `Result<T>` plus a single status→fallback mapping replaces all of this.

### O2. Aggregate initialization by position. **Low.** Fix ticket: 04.

`InitializationResult result{false, {}, true, true}` and `{false, loaded.diagnostic, loaded.moduleLoaded, false}` set fields by their position in the struct. Reordering a field silently changes their meaning. Use designated initializers or named assignment.

### O3. A field that is always true. **Low.** Fix ticket: 04.

`ComponentInfo::isCollectible` (`ManagedComponent.hpp:14`) is always true. The managed side always writes 1, and native code rejects anything else. The field and its wire output can be removed.

### O4. `DiscoverHostFxr` is a member but uses no state. **Low.** Fix ticket: 04.

It could be a free function, or stay a member with a comment explaining why.

### O5. The fake bridge selects behaviour with magic numbers. **Medium.** Fix ticket: 02 / 03.

`Tests/DiagnosticBridge/Bootstrap.cs` selects behaviour by magic argument values: `left == 1..12`, `stringMode 1..26`, and `100..103`/`200..240` as control channels. It also has to mirror every production export signature. After 02, a fake bridge only needs to fill a table, so it can become a small set of named scenarios.

---

## Test gaps / test quality

### T1. Managed checks run in a different load-context arrangement. **Medium.** Fix ticket: 02.

See D1. The managed checks call the UnmanagedCallersOnly exports in-process with the bridge in the default context. The native checks cover the real arrangement, so add an explicit native check for whatever bridge placement 02 decides on.

### T2. Each test executable is one long script. **Medium.** Fix ticket: 04.

`components.cpp`, `diagnostics.cpp` and the `ManagedDiagnostics` program each run dozens of contracts and stop at the first failing `Check`. A failure only reports a message, and nothing after it runs. Either split them into more CTests or adopt a small framework. The engine already vendors googletest through assimp, but the hosting core must stay engine-independent, so a FetchContent'd doctest/Catch2 or plain per-case executables are more appropriate.

### T3. Test names don't match their content. **Low.** Fix ticket: 04.

`ManagedDiagnostics` also contains the component, invocation, dependency and unload checks. `NativeDiagnostics-*` mostly exercises demo calls. Rename them as part of the moves in 03 and 04.

### T4. Linux and macOS have never been run. **Info.** Fix ticket: 18.

Expected, given that CI is deferred. Report it honestly in the stabilisation platform matrix.

---

## Build / packaging

### B1. The example and the tests build the managed side differently. **Medium.** Fix ticket: 05.

The example needs a manual `dotnet build` of the bridge, while the tests build it through a CMake custom target. Consumers need one documented path, ideally CMake building or locating the bridge.

### B2. The managed test build reruns on every build. **Low.** Fix ticket: 05.

`FenrirManagedDiagnosticChecksBuild` is an `ALL` custom target with no outputs, so five `dotnet build`/`publish` commands run every time anything is built.

### B3. Naming is tied to the engine. **Info.** Fix ticket: 05.

The names are `FenrirDotNetHost`, `Fenrir::Scripting` and `Fenrir.ManagedHost`. Ticket 05 decides whether to keep the Fenrir prefix for a library meant to be reusable.

---

## Comment / documentation noise

### N1. The README is 857 lines. **Medium.** Fix ticket: 04.

It narrates each milestone ("Integer interop and managed errors", "Private host-string encoding", and so on), repeats the header comments, and duplicates the plan. Cut it to a usage guide (build, configure, run, lifetime rules). Move rationale into ADRs.

### N2. Header comments repeat themselves and hedge. **Medium.** Fix ticket: 04.

For example, every `DotNetHost` method repeats the serialization rule and the 4096-byte diagnostic contract. Several comments describe implementation steps (the query, then allocate, then copy sequence) rather than the contract. State each contract once, at the type or module level.

### N3. The example program is mostly printing. **Low.** Fix ticket: 03.

`Examples/Hosting/main.cpp` is 275 lines. Rewrite it as a short demonstration of start-up, load, call and release.

### N4. The plan was out of date. **Info.** Fix ticket: 01.

It forbade reflection and marshalling, and its "next step" pointed at milestone 5. It has been rewritten in this ticket.

---

## Summary by ticket

| Ticket | Findings |
| --- | --- |
| 02 | D1, D2, O5 (partly), T1 |
| 03 | D3, C5 (partly), O5, N3 |
| 04 | D6, C1, C2, C3, C4, O1, O2, O3, O4, T2, T3, N1, N2 |
| 05 | B1, B2, B3, D4 (or a new ticket) |
| Later API tickets | D5 → 17, D7 → 09, C5 → 12, T4 → 18 |
| New ticket? | D4: shared contract assemblies |
