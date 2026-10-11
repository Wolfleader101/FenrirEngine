# Review 18: Iris public API as of `2284f54`

Scope: everything a C++ project sees of Iris: the ten public headers in `Native/include/Iris`, the CMake package, the README, and what a component author has to write on the managed side. The managed bridge and the native sources are reviewed only where they decide public behaviour. It is reviewed against three questions:

1. Is the surface consistent enough to freeze (naming, result shapes, ownership, statuses)?
2. How does it compare with Mono's embedding API and Coral, the two references named in the spec?
3. How easy is it to use for the job it is going to do first: a script system in FenrirEngine (ticket 19)?

**Baseline:** a clean build passes all 12 CTests in Debug and Release on Windows x64 (MSVC 19.44, .NET SDK 10.0.401). Linux and macOS have never been run.

**Severity:** as in review 01. **High** means wrong behaviour, or something that will be expensive to change once frozen. **Medium** means real friction or risk. **Low** means a smell. **Info** means context.

**Disposition:** *Fixed* (in ticket 18), *Deferred* (additive, so it can land after the freeze without breaking anyone; a suggested follow-up ticket is named), or *19* (a decision for the engine integration spec).

The rule used to decide between fixing and deferring: **fix now anything that would be a breaking change later; defer anything additive.** A freeze is only worth something if the things it freezes are right. Missing features cost nothing to add later.

---

## Verdict

The API is in good shape to freeze. Its core ideas are consistent across all twelve tickets, and all of them are worth keeping:

- Results instead of exceptions.
- Typed handles resolved once by exact signature, then called many times.
- Ownership as move-only RAII owners (`Component`, `Object`, `Array`), while lookups are cheap copyable values (`Type`, `Method`, `Member`).
- Release kept separate from collection.

Nothing found is a correctness bug in shipped behaviour. The problems are of four kinds:

- **Leftovers that would be breaking to remove later.** These are the dead `BufferTooSmall` status, a public `Array::FromObject` marked "for library use", and private bridge helpers declared in a public header.
- **Docs that lagged behind tickets 12–16.** Several header comments and the README still say "only primitives compile". The compile-time error message says the same.
- **Ergonomic friction.** Every result needs `.value->`, there is no single include, and the two `Initialize` result types read differently from every other result.
- **Missing features compared with Mono and Coral.** None of these block a freeze, because all of them are additive. Two matter for the engine: member enumeration (for an editor inspector) and a way to wait for an unload. Ticket 19 should decide them.

## Keep (do not "clean up" these away)

- **Results, never exceptions.** Every call returns a `CallResult<T>` whose status is never derived from an exception type. A managed exception is data (`ManagedException` plus its `ToString()`), not control flow. Mono uses an out-parameter and Coral uses a process-wide exception callback. Iris's shape is the most robust of the three, and the log sink still gives the process-wide view.
- **Find once, call many.** A lookup matches the C++ signature exactly at `Find*` time, so `Invoke` checks nothing but the handle. Coral resolves by name and argument count on each call, and Mono needs a method lookup, then boxed arguments for `mono_runtime_invoke`. Iris catches signature mistakes at lookup, and unsupported types fail at compile time.
- **Two kinds of public object.** Owners (`Component`, `Object`, `Array`) are move-only and release on destruction. Handles (`Type`, `Method`, `InstanceMethod`, `Member`) are copyable values that root nothing. The rules are the same on every type.
- **Release is not collection.** Release is explicit. Collection is observed (`NotRequested`, `Pending`, `Collected`) and never forced by library code.
- **Handles are never reused, and validated on every call.** A stale handle can't select anything in a new load (proved in ticket 17).
- **Strict text.** UTF-8 in and out of every call, malformed input rejected rather than replaced, and null distinct from empty. Read-only metadata snapshots are the deliberate exception: a lone surrogate there becomes U+FFFD, so one odd attribute argument cannot fail a whole query. This is documented.
- **Internal calls bind at load, or the load fails.** Managed code can never call a null native pointer.

---

## Consistency (naming, result shapes, ownership, statuses)

### A1. `CallStatus::BufferTooSmall` cannot happen through the public API. **High** (breaking to remove later). *Fixed.*

`BufferTooSmall` came from the old query-then-copy string protocol. Every string a caller can pass or receive now travels as a block in one call (ticket 12). The query-and-copy path survives only for the two name reads, `Component::GetInfo` and `Type::GetInfo`, and there native code sizes the buffer from the bridge's own answer. A "too small" reply would be the bridge contradicting itself, which is a malformed response.

**Fix:** removed from the public `CallStatus`. Wire status 4 now maps to `InvalidResponse` with a diagnostic, and the managed `BridgeStatus` keeps it. No bridge ABI change.

### A2. `InitializationResult` and `HostFxrDiscoveryResult` read differently from every other result. **Low.** *Fixed (additive).*

They have a `success` field where every `CallResult` has `Succeeded()`. They are separate types on purpose: initialization has no `CallStatus`, and has progress flags. But the spelling at the call site needn't differ.

**Fix:** both gain `Succeeded()`. `success` stays, since removing it would be breaking.

### A3. Validity predicates are named per concept. **Info.** *Kept.*

`Component::IsLoaded()`, `Object::IsValid()` and `Array::IsValid()`. Handles have no predicate, because they can't be empty (see E3). The two names describe different things: a component is loaded or released, while an object owner holds an object or not. They are kept, and the glossary records both.

### A4. Ownership and status rules are consistent. **Info.** *No change.*

Checked type by type:

- `Release()` is idempotent on every owner.
- Destroying an owner never throws.
- A moved-from owner is empty: its calls report `InvalidHandle`, and releasing it again succeeds.
- An argument from another component is `InvalidArgument`.
- A released or moved-from argument is `InvalidHandle`.
- A missing lookup is `NotFound` and is not logged.
- Misuse that the signature can't prevent (a static member used with an object, writing a read-only member) is `InvalidArgument` with a diagnostic.

One deliberate asymmetry: `Component::Release` refuses with `InUse`, but `Object::Release` cannot refuse. This is recorded in ADR-0005.

### A5. Thread-safety statements disagree with each other. **Medium.** *Fixed (docs); behaviour is 19.*

- `IMPLEMENTATION_PLAN.md` puts thread safety out of scope ("calls on a wrapper or owner must be externally serialized").
- `Log.hpp` says bridge calls "may come from several threads".
- `InternalCall.hpp` says functions must be thread-safe "if components run on several".

In fact, the managed registry guards its tables with process-wide locks, taken briefly around each lookup and never while component code runs, and both native process-wide tables are mutex-guarded. So calls on *different* owners from different threads are designed to be safe. But nothing tests that.

**Fix:** one "Threading" section in the README states what is guaranteed (serialize calls per owner and per host), what is designed but unverified (different owners on different threads), and what is unknown (scripts that start their own threads). Ticket 19 decides the engine's threading model and whether Iris needs a concurrency test suite.

---

## Header hygiene (no SDK types, OS handles or managed implementation details)

### H1. No SDK or OS types leak. **Info.** *Verified.*

None of `hostfxr.h`, `nethost.h`, `coreclr_delegates.h`, `char_t`, `HMODULE`, `void*` handles or `GCHandle` appear in `include/Iris`. Paths are `std::filesystem::path`. `IRIS_INTERNAL_CALL` is `__cdecl` only on 32-bit Windows.

### H2. `Detail::TakeString` and `Detail::TakeBytes` take the private `BridgeFunctions` table in a public header. **Medium.** *Fixed.*

They are declared in `Method.hpp` with a forward-declared `Detail::BridgeFunctions`, but only `.cpp` files call them.

**Fix:** moved to a private `StringBlock.hpp`. What remains in public headers under `Detail` is only what the header templates themselves need: wire kinds, `KindSpec`, the struct mapping description and the `Raw*` classes. After the second pass (S5), every `namespace Detail` block says it is not API. The wire-format types stay inline because the templates need them, so a consumer must build against the headers of the library it links.

### H3. `Array<T>::FromObject` is public, unchecked, and documented as "for library use". **Medium** (breaking to remove later). *Fixed.*

A caller could wrap any `Object` as any `Array<T>`. The first element access fails safely (`InvalidArgument`), but a public function the docs tell you not to call is an API smell. It would be breaking to remove after the freeze.

**Fix:** it is private now. The returned-array path reaches it through `Detail::ArrayAccess`, and so does the one test that checks the defensive path.

### H4. The compile-time error for an unsupported type is out of date. **Low.** *Fixed.*

It says "Only bool, sized integers, float, double and Object cross the boundary by value", which has been incomplete since ticket 12. It now names everything that can cross. The `UnsupportedTypeDoesNotCompile` check matches the new text.

### H5. There is no single include. **Low.** *Fixed (additive).*

You have to know that `Host.hpp` pulls in most headers, but `Array.hpp` and `InternalCall.hpp` must be included separately. A signature that mentions `Array<T>` fails with an incomplete-type error if `Array.hpp` is missing.

**Fix:** `#include <Iris/Iris.hpp>` includes everything. The individual headers stay usable.

---

## Ease of use

The test is writing the script system ticket 19 will need:

1. start the runtime;
2. load a game assembly;
3. find every type implementing `IScript`;
4. create one instance per entity;
5. call `OnUpdate(float)` every frame;
6. expose engine functions;
7. reload on rebuild.

Every step is possible today with the public API alone. The friction is:

### E1. Every access to a result's value goes through `.value`. **Medium.** *Fixed (additive).*

Code reads `type.value->FindMethod<...>(...)`, `*sum.value` and `std::move(*loaded.value)`. The pattern is safe but noisy, and it appears on almost every line of a host.

**Fix:** `CallResult<T>` gains `operator*` and `operator->` (with `&&` overloads to move out), with the same precondition as `std::optional`: only on success. `component.FindType("X")->FindMethod<...>("Y")` now reads like `std::expected`.

There is deliberately no `operator bool`. `CallResult<bool>` (from `IsAssignableTo`) would make `if (type.IsAssignableTo("X"))` test success rather than the answer, which is an easy, silent mistake.

### E2. Many features need their own header, and none is discoverable from `Host.hpp`. *See H5.*

### E3. Handles and owners cannot be default-constructed. **Medium.** *Deferred: "Empty handles".*

A script record that caches `Method<void(float)> onUpdate;` has to use `std::optional<Method<...>>`, because there is no empty state. Coral's types are default-constructible. Adding a default constructor that makes an empty handle reporting `InvalidHandle` is additive, so it doesn't block the freeze. Ticket 19 will show how often it hurts.

### E4. Component authors write raw interop for internal calls. **Low.** *Deferred: an engine API concern (19).*

A string reaches native code as `byte*` and a length, and the component encodes it with `Encoding.UTF8`. A `bool` is rejected. This is the right layer for a hosting library. An engine's scripting API assembly (a shared contract) is where friendly wrappers belong.

### E5. A component does not need to reference `Iris.Bridge`. **Low.** *Fixed (docs).*

The README said "Components reference the bridge with `Private=false`", which reads as a requirement. Only the example component references it, to test resolution. The README now says no reference is needed, and gives the settings to use if you add one.

### E6. The log sink can only be set after `Initialize`. **Low.** *Deferred: "Log sink before initialization".*

The messages `Initialize` itself produces (shared assemblies) are lost. Internal calls already show the pattern that fixes this: store the registration and push it when the bridge comes up. Turning `NotInitialized` into success is additive.

### E7. One-off calls are verbose. **Info.** *Kept.*

Calling a method once takes `FindType`, `FindMethod` and `Invoke`. Coral offers `InvokeMethod("Name", args...)`. Iris's shape is the right default for an engine, which looks methods up once and calls them every frame. A convenience on top would be additive.

---

## Performance

### P1. One call costs about 120–180 ns. **Info.** *Measured; faster invocation deferred: "Fast invocation path".*

Measured on Windows x64 in Release, 1,000,000 calls after a warm-up, through the public API:

| Call | Before | After P2 |
| --- | --- | --- |
| `static int Add(int, int)` | 135 ns | 121 ns |
| `instance int Next()` | 146 ns | 118 ns |
| `static string Echo(string)` | 255 ns | 178 ns |
| `FindType` (lookup, not per frame) | 547 ns | 614 ns (noise) |

Most of the cost is managed:

- `MethodInfo.Invoke` with boxed arguments;
- an `object[]` allocated per call;
- a dictionary lookup under the registry lock.

That is the same order as Coral, which also invokes through reflection. At 10,000 script calls a frame it is about 1.2 ms. A compiled-invoker fast path (one delegate per method, no boxing) is an internal change behind the same `Invoke`, so it can come later without breaking anything. Ticket 19 should set a call budget.

### P2. Every bridge call zeroed a 4 KiB diagnostic buffer. **Low.** *Fixed.*

`ManagedDiagnosticBuffer` value-initialized its `std::array<uint8_t, 4096>`, which means a 4 KiB memset per call, including successful ones. The bridge writes only `written` bytes and only those are read, so it is no longer zeroed. The calls above got measurably cheaper. The one-off numbers are within the noise for `FindType`, so treat the size of the gain as approximate.

---

## Compared with Mono and Coral

Coral and Mono are architectural references only (ADR-0001). The comparison is by capability, from their public documentation and headers; nothing was copied.

| Capability | Mono embedding | Coral | Iris |
| --- | --- | --- | --- |
| Start the runtime | `mono_jit_init` | `HostInstance::Initialize` | `Host::Initialize` (hostfxr + nethost), with progress flags |
| Isolated, unloadable code | domains (legacy) | `AssemblyLoadContext` | one collectible context per `Component`; unload observed, never forced |
| Unload verification | none | `GC::Collect` exposed | `GetUnloadState` (Pending/Collected); no way to force or wait (**gap G4**) |
| Errors | exception out-parameter | exception callback | `CallResult` status + diagnostic, plus a log sink |
| Find a type | `mono_class_from_name` | `GetType(name)` | `FindType(fullName)` |
| List types | metadata tables | `GetTypes()` | `GetTypes()` |
| Base, interfaces, assignability | yes | yes | yes |
| Type attributes | `mono_custom_attrs_*` | yes | yes, read from metadata without running code |
| Member attributes, member listing | yes | `GetMethods/GetFields/GetProperties` | **gap G1** |
| Call static/instance methods | by `MonoMethod*`, boxed args | by name + arg count per call | by exact typed signature, resolved once |
| Virtual dispatch | yes | yes | yes |
| Construct objects | `mono_object_new` + ctor | `CreateInstance(args...)` | `CreateObject(args...)`, exact constructor |
| Object lifetime | GC handles by hand | `ManagedObject::Destroy` | RAII `Object`; release blocks while objects live |
| Object's runtime type | `mono_object_get_class` | yes | **gap G2** |
| Fields and properties | yes | yes | yes, typed, static and instance, misuse diagnosed |
| Strings | `MonoString*` + conversions | `Coral::String` | UTF-8 `std::string_view` in, `std::optional<std::string>` out, strict |
| Structs | boxed / unboxed | by value | by value, layout validated against managed metadata at lookup |
| Arrays | `MonoArray*` | `Coral::Array` | `Array<T>` owner, bulk copy, null vs empty |
| Enums | as underlying | as underlying | **gap G3** |
| Managed → native calls | `mono_add_internal_call` (`extern` methods) | function pointer fields, uploaded | function pointer fields, bound and checked at load |
| Generics, delegates, events, async | yes | partly | out of scope (spec) |
| Thread attach | `mono_thread_attach` required | automatic | automatic (CoreCLR reverse P/Invoke) |
| Debugger | soft debugger | standard .NET debugger | standard .NET debugger (untested) |

Where Iris is ahead:

- Signatures are checked at lookup and at compile time.
- Struct layouts are validated, not trusted.
- A load with an unbound internal call fails up front.
- Results carry managed diagnostics.
- Reload safety (stale handles, no Iris-held roots) is tested.

The real gaps:

### G1. Member enumeration and member attributes. **Medium.** *Deferred (19 decides the priority): "Member enumeration and attributes".*

You cannot list a type's methods, fields or properties, or read attributes on them. An editor inspector (`[SerializeField]`, `[Range(0, 1)]`) needs both. This is additive: new snapshot queries on `Type` that reuse ticket 16's snapshot machinery.

### G2. An `Object` cannot report its runtime type. **Low.** *Deferred: "Object type".*

A method returning an interface gives an `Object` whose concrete type is unknown. An additive `Object::GetType()` returning a `Type` would fix this.

### G3. Enums cannot be arguments, returns, fields or struct fields. **Medium.** *Deferred: "Enums".*

Gameplay code uses enums a lot. They can cross as their underlying integer, matched exactly. This is additive.

### G4. A host cannot wait for an unload to finish. **Medium.** *Deferred (19 decides): "Unload wait".*

Reload code that wants to confirm the old version is gone, or to warn when a script leaks a root, can only poll `GetUnloadState`, and nothing ever triggers a collection. The library rule "never force GC" was right for implicit behaviour. An explicit, documented `Host::CollectGarbage()` or a `WaitForUnload(timeout)` is a host decision, and is additive.

---

## Documentation

### D1. Header comments lag behind tickets 12–16. **Medium.** *Fixed.*

These still described the ticket 07–09 surface ("only `PrimitiveValue` types compile"), and left out strings, structs and arrays:

- `Type::FindMethod`, `Type::CreateObject` and `Type::FindField`;
- the `Argument`, `SignatureArgument` and `Returnable` concepts;
- `MemberValue` and `Member`.

All are updated.

### D2. README gaps. **Medium.** *Fixed.*

- The "Methods" section said only primitives compile.
- The checks table is missing `Components-unbound` and `UnsupportedTypeDoesNotCompile`.
- There is no threading section (A5) and no platform matrix (T1).
- Deployment requirements don't mention shared assemblies or registering internal calls before loading.
- The status table lists `BufferTooSmall`.
- There is no comparison for readers coming from Mono or Coral.

### D3. There is no glossary. **Medium.** *Fixed.*

`GLOSSARY.md` at the repository root (the location `docs/agents/domain.md` uses) defines the core terms. "Generation" in tickets 06, 07 and 11 is defined there as one load of a component, identified by its token. The code never uses the word.

### D4. The plan's "Current state" stops at ticket 16. **Low.** *Fixed.*

---

## Tests and platforms

### T1. Only Windows has been run. **Info.** *Recorded; Linux deferred to a follow-up run.*

The platform matrix in the README is honest:

- Windows x64 / MSVC: Debug and Release verified.
- Linux and macOS: the CMake logic exists, but nothing has ever been built or run.

A WSL Ubuntu 24.04 environment with g++ 13 and CMake 3.28 is available on the development machine, but it has no .NET SDK. Installing one is the next step for a Linux row. CI stays future work (spec).

### T2. No concurrency test. **Info.** *19.* See A5.

### T3. No performance check. **Info.** *Deferred.* The numbers in P1 came from a one-off measurement through the public API. A benchmark target belongs with the fast-path ticket, because that ticket needs a baseline.

---

## Second pass: review of the first fix commit

An independent review of `00817d7` found more things the freeze would have locked in. All were fixed in the follow-up commit unless marked deferred.

### S1. `Array::AsObject()` was public plumbing. **Medium.** *Fixed.*

It has the same smell as H3, and it let an array be passed where an `Object` was expected. It is now private, reached through `Detail::ArrayAccess::ObjectOf`.

### S2. `*result` on a temporary could dangle. **Medium.** *Fixed.*

The `&&` overload returned `T&&`. In C++20, `for (const Type& t : *component.GetTypes())` binds a reference into a destroyed `CallResult` (P2718 is C++23). The rvalue overload now returns `T` by value. That costs one move, and nothing dangles. Changing it after the freeze would alter `decltype`, so it had to happen now. Every dereference also `assert`s that the value is present.

### S3. `InvalidArgument` covered returns too. **Medium.** *Decided, documented.*

Two cases are `InvalidArgument` although no argument was invalid:
- a returned string that cannot become UTF-8, or exceeds 2 GiB;
- an ambiguous object-overload lookup.

Rather than splitting the status, its frozen meaning is stated (README status table, ADR-0006): a value that cannot cross the boundary in either direction, a malformed or ambiguous name, or a misused call.

### S4. `GetUnloadStatus()` returned a `ComponentUnloadState`. **Low.** *Fixed.*

It is renamed `GetUnloadState`, which matches the enum and the bridge's `GetComponentUnloadState`.

### S5. `Detail` was not marked as not API, although H2 and ADR-0006 said it was. **Low.** *Fixed.*

Every block now has a banner.

### S6. The consumer dereferenced unchecked results, and "every feature" was overclaimed. **Medium.** *Fixed.*

Every step is checked through small helpers. The claims now say "a small script host". The README says plainly that its examples skip the checks.

### S7. Doc corrections. **Low.** *Fixed.*

- the lock wording (A5);
- the string array-element row;
- internal-call types ("a different set", not "smaller");
- "throws only on allocation or OS text conversion failure";
- metadata U+FFFD;
- the moved-from wording;
- stale `Object` and `PrimitiveValue` comments;
- examples for `DiscoverHostFxr`, `IsInitialized`, `sharedAssemblies`, `GetInfo` and `GetUnloadState`;
- softer P2 numbers.

### S8. More additive gaps. **Low.** *Deferred.*

- **Nullable object arguments.** There is no way to pass null to a class parameter. Arrays have `const Array<T>*` and members take `const Object*`, so a `const Object*` signature argument is the natural addition.
- **Comparison and hashing.** `operator==` and hashing on `Type` and `Method`, for engine maps keyed by type, plus object identity comparison.
- **Exception type as data.** The managed exception's type name is available today only inside the `ToString` text.
- **Convenience helpers.** `CallResult::value_or`, and `ToString(CallStatus)`.
- **A movable `Host`.**
- **A free `SetLogSink`,** like `RegisterInternalCall`. This would also fix E6.

---

## Summary

| Disposition | Findings |
| --- | --- |
| Fixed in 18 | A1, A2, A5 (docs), H2, H3, H4, H5, E1, E5, P2, D1, D2, D3, D4; second pass S1–S7 |
| Kept, by design | A3, A4, E7, H1 |
| Deferred, additive | E3 empty handles, E6 log sink before init, P1 fast invocation, G1 member enumeration and attributes, G2 object type, G3 enums, T3 benchmark, S8 (nullable object arguments, equality and hashing, exception type, helpers, movable `Host`, free `SetLogSink`) |
| For ticket 19 | A5 threading model, G1 and G4 priorities, E4 engine API wrappers, P1 call budget, T2 |
| Platform | T1: Windows only; Linux next, macOS and CI later |
