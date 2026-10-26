# 01: FenrirScripting: per-call context, NativeGuard, helpers, the zeroing template, the test harness, ADR-0045 and the glossary

**What to build:** Everything the six ports and the generator change rest on, in FenrirScripting alone (spec: "A per-call context that knows its native", "NativeGuard", "Marshalling helpers for natives", "The registration zeroes outputs", "A test seam for natives"). No package extension changes here; each still compiles against the old `WarnOnceRecord::Warn(context, message)` since its shape is kept.

- **Per-call `CallContext`** (`include/FenrirScripting/ScriptingExtension.hpp`): gains the native's name as a `std::string_view`, `Native()`. `Detail::NativeFunctionCall` gains a static `name` beside `state` and `notRunning`, set by `NativeFunction::Register` to the type-and-method part of the registered name (`PhysicsNative.Raycast` from `Fenrir.Physics.PhysicsNative.Raycast`), and `Call` builds a `CallContext` on the stack from the active engine context's App and change buffer plus that name. `EngineContext::call` goes, or keeps only the App and buffer the thunk copies from; `Detail::ActiveCallContext()` returns what the thunk needs. The built-in extension's natives in `src/Bindings.cpp` receive the per-call context unchanged in behaviour.
- **`WarnOnceRecord::Warn(const CallContext&, message)`** prefixes `<native>: ` from the context. The `App&` overload stays for the few callers that have no context.
- **`NativeGuard`** (`include/FenrirScripting/NativeGuard.hpp`): constructed from a `const CallContext&` and a `WarnOnceRecord&`. `Alive(EntityValue) -> std::optional<Entity>` (the entity of the active scene, or empty with a warning once), `Require(pointer-or-optional, message)` passing its argument through and warning once when empty, `Warn(message)` warning once for its call site. Each takes `std::source_location::current()` as its last defaulted parameter so "once" means once per place, as today.
- **Marshalling helpers** (`include/FenrirScripting/NativeMarshalling.hpp`): `SpanOf(first, count)` (empty for null or non-positive, the one clang pragma), `WriteText(char* out, int32 capacity, std::string_view text) -> int32` and a `char16_t` form returning the full length, and `Utf8ToUtf16` moved from `packages/FenrirWindow/Scripting/src/Utf16.hpp` (the file itself goes in ticket 06).
- **`ZeroingOutputs<auto Native, std::size_t... Outputs>`** (`include/FenrirScripting/ZeroingOutputs.hpp`): `Call` has the native's signature, assigns `{}` through each pointer parameter at the given indices, then forwards. It is what `NativeFunction::Make` is given, so it sits inside the not-running check.
- **`Fenrir::Testing::NativeCallHarness`** (`Tests/NativeCallHarness.hpp`, shared with package tests through `fenrir_scripting_tests_dir` like `ScriptingProbe.hpp`): owns a `Detail::ChangeBuffer` over a `HeadlessApp`, is a friend of `CallContext`, gives `Context(std::string_view native)` and `ApplyChanges()`. It includes `src/ChangeBuffer.hpp`; document that package scripting tests add FenrirScripting's `src` include, as the Rules tests do.
- **ADR-0045** `docs/adr/0045-natives-trust-the-generated-declaration-...md`: `in`/`out` pointers are never null, single outputs are zeroed by the registration, spans and `inOut` are untouched, the zeroing sits inside the not-running check. Considered options: per-native checks (today), a guard that zeroes, generated lambdas.
- **`GLOSSARY.md`**: *native guard* under FenrirEngine scripting, in the spec's words, with *Avoid:* checker, validator, precondition.
- **README** (`packages/FenrirScripting/README.md`, the extensions section): how a native is written now, in one example.

**Blocked by:** none

**Touches:** `packages/FenrirScripting/include/FenrirScripting/`, `packages/FenrirScripting/src/`, `packages/FenrirScripting/Tests/`, `docs/adr/`, `GLOSSARY.md`, `packages/FenrirScripting/README.md`

**Status:** ready-for-agent

- [ ] Rules tests (no .NET): `ZeroingOutputs` zeroes the named outputs and forwards every argument and the result; it leaves an output span and an `inOut` pointer untouched; `NativeGuard::Alive` is empty for a dead entity and warns once per call site and message across two calls; `Require` passes a live pointer and an engaged optional through and warns once for null and empty; `Warn` logs once for two calls from one place and twice from two places; `WriteText` fills up to capacity and returns the full length in both encodings; `SpanOf` is empty for null and for a non-positive count.
- [ ] `ScriptingTests` (.NET): a native of the test extension warns with `<Type>.<Method>: ` as its prefix with no name given by the native; a native called while no Scripting runs returns its `@notrunning` value and its output is untouched.
- [ ] `NativeCallHarness` is used by one FenrirScripting test calling a test-extension native directly: the change it records plays back through `ApplyChanges`.
- [ ] `check_tickets.py`, clang-format and the msvc-x64-debug preset pass; no package extension changes.

## Comments

Filed from the native guard grilling (spec decisions 1, 4, 5, 7, 9, 11, 12, 13, 15). Ticket 02 adds the generator's side of the zeroing; tickets 03 to 08 adopt the guard.
