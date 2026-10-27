# 02: Iris.Bindings and describe-api: the registration zeroes outputs, and the per-native defaults tests go

**What to build:** The generated registration names FenrirScripting's `ZeroingOutputs` template for every native with a single `[out]` pointer, and the generated tests that proved each native zeroed its own outputs are deleted (spec: "The registration zeroes outputs"; ADR-0045 from ticket 01).

- **One new key** in the binding configuration's `[natives]` section, `zero-outputs` (name per `BindingConfig.cs`'s conventions), naming the template: `"Fenrir::ZeroingOutputs"`. Required, like `make`; Iris.Bindings keeps no engine name of its own. Documented in `packages/Iris/Tools/Iris.Bindings/README.md` and `packages/Iris/docs/natives-description.md`.
- **`CppWriter.cs`** emits, for a native with outputs at parameter indices `i, j`, `Make<&ZeroingOutputs<&Ns::Native, i, j>::Call>("Full.Name"[, notRunning])`; for a native with none, the line it emits today. Indices count the native's full parameter list (context and state included), so the template needs no knowledge of the state. An output that the description records as a span (a pointer followed by a count, not marked `[in]`) or as `inOut` is not an output here.
- **Delete** the `natives-cpp`, `natives-csharp` and `natives-class` keys from `BindingConfig.cs`, the writers that produce `*NativesTests.cpp` and `*Natives.cs`, and the fixture's copies under `packages/Iris/Tools/Iris.Bindings/tests/Fixture/`. A configuration still carrying the keys fails with a diagnostic naming them.
- **Regenerate** every binding (`cmake --build <dir> --target fenrir-generate-bindings`): the eight `Generated/*Registration.cpp` files gain the zeroing lines; the six `Tests/Generated/*NativesTests.cpp` and `Tests/Fixture/.../*Natives.cs` files are deleted, with their `CMakeLists.txt` and fixture `.csproj` lines; the eight `*.binding.toml` files gain `zero-outputs` and lose the three test keys. The regenerated natives still zero their own outputs by hand in this ticket; the ports remove those lines.
- **Iris.Bindings tests** (`tests/BindingTests.cs`): the fixture's registration shows one zeroing line for a native with an output, none for a native with only inputs, none for an output span, none for `inOut`; the fixture includes a native with two outputs.

**Blocked by:** 01

**Touches:** `packages/Iris/Tools/Iris.Bindings/`, `packages/Iris/docs/natives-description.md`, `packages/*/Scripting/api/*.binding.toml`, `packages/FenrirScripting/api/*.binding.toml`, `packages/*/Scripting/src/Generated/*Registration.cpp`, `packages/*/Scripting/Tests/Generated/`, `packages/*/Scripting/Tests/Fixture/`, `packages/*/Scripting/Tests/CMakeLists.txt`

**Status:** resolved

- [x] `dotnet test` for Iris.Bindings passes with the new cases; `fenrir-generate-bindings` is a no-op after the committed regeneration (CI's api-descriptions check).
- [x] No `*NativesTests.cpp` or `*Natives.cs` remains under any `Scripting/Tests/`; CTest lists no `writes its output's default` cases.
- [x] Every package scripting test target still builds and passes on the msvc-x64-debug preset: the natives behave as before since they still zero by hand.

## Comments

Filed from the native guard grilling (spec decisions 3, 8, 9, 10). This ticket touches every package's `Generated/` registration, so it merges before any of tickets 03 to 08 starts.
