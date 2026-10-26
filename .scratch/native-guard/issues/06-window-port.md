# 06: Port the window extension onto NativeGuard, and retire its private Utf16.hpp

**What to build:** Every window native becomes its one call behind the guard, and the text helpers it kept privately are FenrirScripting's (spec: "NativeGuard", "Marshalling helpers for natives").

- **`src/WindowScriptingState.hpp`**: the window lookup loses its `std::string_view function` and `source_location` parameters and warns through a `NativeGuard` made from the context and `warnings`. The 12 hand-typed facade names (`"Window.SetCursorMode"`, ...) go.
- **`src/WindowScripting.cpp`**: remove the `== nullptr` checks on `in` and `out` parameters and the `*out = T{}` lines. `GetClipboard`'s hand-rolled fill-up-to-capacity (lines 158-169 today) becomes `WriteText`. The enum range checks for cursor mode and shape stay. `src/Utf16.hpp` is deleted; callers use `Utf8ToUtf16` from `FenrirScripting/NativeMarshalling.hpp` (ticket 01).
- **Tests**: the no-window cases in `Tests/WindowScriptingTests.cpp` (a native with no window of that id returns its default and warns once; an unknown cursor mode does nothing and warns once) move to a `Tests/Rules.cpp` on `NativeCallHarness` with a `WindowScripting` state the test constructs; the rest stays on .NET. `Tests/CMakeLists.txt` adds FenrirScripting's `src` include.

**Blocked by:** 01, 02

**Touches:** `packages/FenrirWindow/Scripting/src/`, `packages/FenrirWindow/Scripting/Tests/`

**Status:** ready-for-agent

- [ ] `src/Utf16.hpp` no longer exists; no facade-name string literal remains in `src/WindowScripting.cpp` or the state header.
- [ ] `GetClipboard` through the harness with a two-character capacity returns the full length and fills two characters (a rule test, no window needed when the clipboard text is given by a fake or the test skips on a headless host as the existing test does).
- [ ] Rule tests pass with no `FENRIR_TEST_WINDOW_FIXTURE` set; the remaining .NET tests pass with it.
- [ ] clang-format and the msvc-x64-debug preset pass.

## Comments

Filed from the native guard grilling (spec decisions 2, 4, 6, 12, 14). Runs side by side with tickets 03 to 05 and 07 once 01 and 02 are merged.
