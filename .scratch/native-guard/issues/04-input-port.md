# 04: Port the input extension onto NativeGuard

**What to build:** Every input native becomes its one call behind the guard (spec: "NativeGuard", "Rule tests move onto it").

- **`src/InputScriptingState.hpp`**: the device lookups (keyboard, mouse, gamepads, the claims resource) lose their `std::string_view function` and `source_location` parameters and warn through a `NativeGuard` made from the context and `warnings`. The 19 hand-typed facade names (`"Keyboard.IsDown"`, ...) go.
- **`src/InputScripting.cpp`**: remove the `== nullptr` checks on `in` and `out` parameters (23 today) and the `*out = T{}` lines; spans through `SpanOf`. The enum range checks that exist because a C# enum value may be none of the enumerators stay: they are a rule, not a null check.
- **`src/InputClaimsResource.cpp`**: unchanged in behaviour; it refreshes the copy as ADR-0043 says. If it warns, it warns through the guard.
- **Tests**: `Tests/NoInput.cpp` (no devices installed: each query returns its default and warns once) moves onto `NativeCallHarness` with an `InputScripting` state the test constructs; `Devices.cpp`, `Keys.cpp` and `Shapes.cpp` stay on .NET. `Tests/CMakeLists.txt` adds FenrirScripting's `src` include.

**Blocked by:** 01, 02

**Touches:** `packages/FenrirInput/Scripting/src/`, `packages/FenrirInput/Scripting/Tests/`

**Status:** resolved

- [x] No facade-name string literal remains in `src/InputScripting.cpp` or the state header; no `source_location` outside guard calls.
- [x] `NoInput` rule tests pass with no `FENRIR_TEST_INPUT_FIXTURE` set; the remaining .NET tests pass with it.
- [x] clang-format and the msvc-x64-debug preset pass.

## Comments

Filed from the native guard grilling (spec decisions 2, 4, 6, 14). Runs side by side with tickets 03 and 05 to 07 once 01 and 02 are merged.
