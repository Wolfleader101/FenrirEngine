---
status: accepted
---

# Natives trust the generated declaration: `in` and `out` pointers are never null, and the registration zeroes single outputs

Every package native ported onto the generated pipeline (ADR-0043, ADR-0044) opened with the same prologue: a null check on each pointer parameter, and a line writing each output's default before any early return, with a generated test per native proving, through C#, that it did. Physics checked every pointer; DebugDraw checked none and dereferenced one unchecked. The generated C# declaration already settles both: a `const T*` or `T*` parameter is declared `in T` or `out T`, which C# cannot pass null for, and an `out` parameter is one the caller reads only after the call.

We decided:

- **`in` and `out` pointers are never null, and natives do not check them.** The generated declaration is the contract: what C# passes as `in T` or `out T` is a reference to a value. A native dereferences its pointer parameters as it would references. The one pointer that can be null is a span's first element, when the span is empty, and `Fenrir::SpanOf` (FenrirScripting's marshalling helpers) is the one place that handles it.
- **Each single `out` parameter is zeroed by the registration, by construction.** The generated registration names `ZeroingOutputs<&Native, indices...>::Call` (FenrirScripting) where it would name the native: it assigns `{}` through each pointer parameter at the given indices (into the full parameter list, call context and extension state counted) and forwards. A native that returns early leaves its outputs at their defaults without writing them, and no per-native test is needed to prove it.
- **An output span and an `inOut` parameter are not zeroed.** An output span is governed by the count the native returns, so what it does not fill is not read; an `inOut` parameter is C#'s value, which the native reads before it writes.
- **The zeroing sits inside the not-running check.** `ZeroingOutputs::Call` has the native's signature and is what `NativeFunction::Make` is given, so the thunk's not-running path returns the `@notrunning` value without touching an output, as it did before.

## Considered options

- **Per-native checks and defaults, as today.** Rejected: the same lines in some ninety natives, one policy where they are missing and dead code where they are not, and a generated test per native to keep them honest.
- **A guard that zeroes.** `NativeGuard` would take the outputs and zero them in its constructor. Rejected: the guard's job is what a call is checked against; zeroing is the registration's business, holds whether or not a native makes a guard, and would still need each native to list its outputs by hand.
- **Generated lambdas.** The registration emits a lambda per native that zeroes and forwards. Rejected: the generator would spell C++ control flow it cannot test, where one template named in one line is tested once in FenrirScripting and makes Iris.Bindings restate nothing but the indices.

## Consequences

- `GLOSSARY.md` gains *native guard*; FenrirScripting gains `NativeGuard`, `NativeMarshalling.hpp` and `ZeroingOutputs.hpp`, and its `CallContext` carries the native's name so warnings name it without a hand-typed string.
- Iris.Bindings names the zeroing template through one binding configuration key, as `make` names `NativeFunction::Make`, and deletes the per-native defaults tests and their keys (`natives-cpp`, `natives-csharp`, `natives-class`) for one C++ test of the template.
- Each package's natives are ported to the contract: no null check on an `in` or `out` parameter, no `*out = T{}` line, no facade name string. A native that needs a span takes it from `SpanOf`.
- The built-in extension's natives in `src/Bindings.cpp` follow the contract like every package's (amended 2026-10-27, `.scratch/builtin-extension`; they were first left out as "keeping their own checks"): the codes they return are `ChangeBuffer`'s and the registry's rules, not pointer checks, and C# throws on them, so they use no native guard either.
