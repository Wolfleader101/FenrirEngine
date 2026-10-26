Status: ready-for-agent

# Native guard: one prologue for every package native, and a C++ test seam for natives

Origin: the architecture review of `dotnet-scripting` against `main` (2026-10-26), candidates 1 (a native guard for package natives) and 7 (natives callable from C++ tests), grilled to eighteen decisions in the same session. The review's other candidates (a component-loader seam under `GenerationLifecycle`, one module for a generation's script data, a generic registry table with generated error codes, span-taking generated declarations, binding defaults inherited through `uses`) are not part of this spec.

The vocabulary is the repo's: *native function*, *call context*, *extension state*, *facade*, *scripting extension* from `GLOSSARY.md`; *module*, *interface*, *seam*, *deep*, *shallow*, *locality*, *leverage* from the codebase-design skill. One hard-to-reverse call is recorded as an ADR that ticket 01 writes:

| ADR | Decision |
|---|---|
| 0045 (new) | Natives trust the generated declaration: `in` and `out` pointers are never null, the registration zeroes each single output, spans and in-out parameters are untouched |

## Problem statement

The six package scripting extensions (physics, assets, renderer, input, window, debug draw: 2,275 lines of natives) were ported onto the generated pipeline in ADR-0043 and ADR-0044. Each native's interface is its call context, its extension state and its arguments, which is deep. Its implementation is not: before the one call it exists to make, every native repeats a prologue that is not a module anywhere.

- **Writing each output's default** before any early return (`*hit = Script::RaycastHit{}`), with a generated test per native, through C#, proving it did.
- **Null-checking pointers** that C# passes as `in T` and `out T`, which are never null. Physics checks every one; DebugDraw checks none and dereferences `options->relativeTo` unchecked. One policy is dead code and the other is missing.
- **Finding its resource or world** (`WorldOf`, `StoreFor`, `TargetFor`, `CameraFor`): these helpers earn their keep and stay.
- **Checking an entity is alive**, written in Renderer (`Alive`) and Physics (`HasBody`) and nowhere shared.
- **Warning once under the native's name.** Five extensions hand-type the C# facade's name per call: 106 strings such as `"Materials.SetFloat"` and `"Keyboard.IsDown"`. Physics instead parses `std::source_location::function_name()` in `PhysicsScripting::NativeName`. The generated registration already spells every native's full C# name and throws it away.
- **Marshalling helpers written twice.** The span-from-pointer-and-count helper, with its clang pragma, is in Assets and DebugDraw. Text-out (fill up to a capacity, return the full length) is in Window and Assets, one of them ASCII-only. `Utf8ToUtf16` is private to FenrirWindow's scripting.
- **Knowledge copied across packages.** Renderer's `SpawnModel` re-implements Assets' "no store, then stale handle" check and its warning text.

The deletion test says the prologue's complexity would reappear in all ~90 natives, so it is a module waiting to exist. The interface is also the only test surface: `CallContext`'s constructor is private to `Scripting`, and the context is active only once a script assembly has loaded, so every test of a native's rules (no world, stale handle, dead entity, each warning once, outputs zeroed) needs .NET, a built fixture assembly (1,155 lines of C#) and one of six near-identical 140-to-270-line test harnesses, and reads its results back through C#.

## Solution

Three small modules in FenrirScripting, one rule moved into the generated registration, one test seam, and six mechanical ports.

### A per-call context that knows its native

`CallContext` becomes a value the registration's thunk builds for each call: the App, the change buffer, and the name of the native being called. The name is the type-and-method part of the native's C# name, `PhysicsNative.Raycast`, with the namespace dropped because the warn-once record already prefixes `[physics scripting]`. The thunk (`Detail::NativeFunctionCall::Call`) takes it from a static set at registration beside `state` and `notRunning`. Nothing about the context's other promises changes: it carries nothing of any extension (ADR-0029), only scripting makes one, and a native is only ever called with the one that is active.

`WarnOnceRecord::Warn(const CallContext&, message)` prefixes the native's name, so a warning reads `[physics scripting] PhysicsNative.Raycast: the active scene has no physics world yet, so it does nothing`. A script author finds `PhysicsNative.Raycast` in the facade's source in one search. The 106 strings and `PhysicsScripting::NativeName` go.

### `NativeGuard`

A value a native makes from its context and its extension state's record:

```cpp
NativeGuard guard(context, physics.warnings);
PhysicsWorld* world = guard.Require(PhysicsWorld::Of(scene), "the active scene has no physics world yet");
if (!guard.Alive(body)) return false;
guard.Warn("the entity has no Joint, so it reads zeroed");
```

- `Alive(EntityValue)` is the one entity-alive rule: the entity of the active scene, or nothing with a warning once.
- `Require(T* or optional, message)` passes its argument through, warning once when it is empty.
- `Warn(message)` warns once for its call site under the native's name.

Each warns once per place and message, as `WarnOnceRecord` does today, in debug builds only, as today. The glossary gains **native guard**: *what a native function checks a call against before acting: its extension's warn-once record and the call context, giving the entity-alive rule, a requirement check and a warning, each naming the native and warning once.*

The package lookups stay on each extension state, built on the guard for their warning: Physics's `WorldOf` and `HasBody`, Assets' `StoreFor`, `Resolve<T>`, `StoreAndOwnerFor` and `OwnerOf`, Renderer's `CameraFor`, DebugDraw's `TargetFor`. Their `std::string_view function` and `std::source_location where` parameters go. Assets moves its model-handle resolve into its public scripting header, and Renderer's `SpawnModel` calls it.

### Marshalling helpers for natives

One FenrirScripting header of free functions, separate from the guard because they are wire shape, not call checks: the span from a first pointer and a count (empty for null or non-positive, with the one clang pragma), text-out into a capacity buffer returning the full length, in UTF-8 and UTF-16, and UTF-8 to UTF-16 moved out of FenrirWindow's private `Utf16.hpp`.

### The registration zeroes outputs

`in` and `out` pointers are never null: the generated C# declaration passes `in T` and `out T`, which cannot be. Natives stop checking them. A span's first pointer is the one pointer that can be null, when the span is empty, and the span helper handles it.

Each single `[out]` pointer is zeroed before the native runs, by construction: Iris.Bindings emits one line per native naming a FenrirScripting template, say `ZeroingOutputs<&PhysicsNatives::Raycast, 6>::Call`, which zeroes the outputs at the given parameter indices and forwards. The template is named through one new binding configuration key, as `make` names `Fenrir::NativeFunction::Make`, so Iris.Bindings stays engine-neutral. An output span (`RaycastAll`'s hits) is governed by its returned count and is not touched; an `inOut` parameter is never zeroed. The zeroing sits inside the not-running check, so a call while no Scripting runs still returns the `@notrunning` value without touching outputs, as today.

With the property held by construction, the six generated `*NativesTests.cpp`, their C# `Natives` fixture classes and the `natives-cpp`, `natives-csharp` and `natives-class` keys are deleted. One Rules-style C++ test of the template replaces them.

### A test seam for natives

`Fenrir::Testing::NativeCallHarness`, in FenrirScripting's shared test headers beside `ScriptingProbe` and `HeadlessApp`, named a friend by `CallContext` as `ScriptingProbe` is by `Scripting`. It owns a `Detail::ChangeBuffer` over a `HeadlessApp` and hands out a `CallContext` for a named native, plays the recorded changes back, and needs no .NET. It includes `src/ChangeBuffer.hpp`, so package scripting tests add FenrirScripting's `src` include as the Rules tests already do. Package extension state is constructed by the test (`PhysicsScripting physics;`).

Rule tests move onto it in each port: no world, no store, stale handle, dead entity, each warning once, defaults on early return. Tests of what a script sees (facades, spans, strings, enums, binding names) stay on .NET. Each port moves only its rule files (`NoOps.cpp` in Assets, DebugDraw and Renderer, `NoInput.cpp` in Input, the no-world cases in Physics) and leaves passing .NET tests alone.

### Out of scope

- The built-in extension's natives in `Bindings.cpp`: they receive the per-call context but report failures as codes that `Entity.Add` and `Resources.Insert` throw on, not as warnings. They do not adopt the guard.
- `WarnOnceRecord` logging in debug builds only (ADR-0043).
- The review's other candidates, including span-taking generated C# declarations and binding defaults inherited through `uses`.

## Decisions (from the grilling)

1. The native's name reaches warnings through a per-call `CallContext` built by the thunk, not `source_location` or strings.
2. `in` and `out` pointers are never null; natives stop checking them; only a span's first pointer is handled.
3. Single outputs are zeroed by the generated registration, by construction.
4. What remains of the prologue is a value, `NativeGuard`, made from the context and the extension's record.
5. The test seam is a harness over a `HeadlessApp` and a change buffer, needing no .NET.
6. Resource and world lookups stay on each extension state; Assets exposes its model resolve for Renderer.
7. The context carries the type-and-method part of the native's name, namespace dropped.
8. Output spans and `inOut` parameters are not zeroed.
9. The zeroing is a FenrirScripting template the generator names in one line per native.
10. The per-native generated defaults tests and their three keys are deleted for one template test.
11. The value is `NativeGuard`; the glossary term is *native guard*.
12. Marshalling helpers are one header of free functions beside the guard, not inside it.
13. The harness is a friend of `CallContext` and includes `src/ChangeBuffer.hpp`, as the Rules tests do.
14. Only rule tests move off .NET; passing .NET tests are left alone.
15. The pointer and zeroing contract is ADR-0045, standing alone.
16. Eight PRs into `dotnet-scripting`: FenrirScripting, the generators, then six ports in parallel, Renderer after Assets.
17. The built-in extension stays out.
18. This spec and its tickets are written first, on a planning branch; the glossary term and ADR land with ticket 01.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | FenrirScripting: per-call context, `NativeGuard`, helpers header, `NativeCallHarness`, `ZeroingOutputs`, ADR-0045, glossary | none |
| 02 | Iris.Bindings and describe-api: the zeroing line and key, the natives tests and keys deleted, outputs regenerated | 01 |
| 03 | Physics port | 01, 02 |
| 04 | Input port | 01, 02 |
| 05 | Assets port, with the public model resolve | 01, 02 |
| 06 | Window port, with `Utf16.hpp` gone | 01, 02 |
| 07 | DebugDraw port | 01, 02 |
| 08 | Renderer port, calling Assets' resolve | 01, 02, 05 |

Tickets 03 to 08 touch disjoint files and run side by side off `dotnet-scripting` once 01 and 02 are merged; 08 waits for 05. Ticket 02 regenerates every package's `Generated/` registration, so it merges before any port starts.

## Done looks like

- Every package native is its one call behind a guard: no hand-typed facade name, no pointer null-check on an `in` or `out` parameter, no `*out = T{}` line, no `source_location` parameter.
- `grep -rn '"[A-Z][A-Za-z]*\.[A-Z][A-Za-z]*"' packages/*/Scripting/src/*Scripting.cpp` finds no facade-name strings.
- Each package's rule tests run with no fixture assembly, through `NativeCallHarness`; CTest still passes the .NET tests it keeps.
- `ADR-0045` is accepted and `GLOSSARY.md` defines *native guard*.
- The msvc-x64-debug preset builds and tests locally; the other presets are CI's.
