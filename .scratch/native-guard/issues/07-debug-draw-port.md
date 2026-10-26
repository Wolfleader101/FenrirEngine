# 07: Port the debug draw extension onto NativeGuard

**What to build:** Every debug draw native becomes its one call behind the guard, with the pointer contract it already assumed now written down (spec: "NativeGuard", ADR-0045).

- **`src/DebugDrawScriptingState.hpp`**: `TargetFor(context, relativeTo)` loses its `std::string_view function` and `source_location` parameters and warns through a `NativeGuard` made from the context and `warnings`; the relative-to entity check uses `guard.Alive`. The 31 hand-typed facade names (`"DebugDraw.Line"`, ...) go.
- **`src/DebugDrawScripting.cpp`**: the natives already dereference `in` pointers unchecked, which ADR-0045 now makes the contract; nothing is added there. The `Points` span helper with its clang pragma (lines 29-45 today) becomes `SpanOf`. The `Grid` native's `@param[in]` cells pointer stays as described (ADR-0044).
- **Tests**: `Tests/NoOps.cpp` (no `DebugDraw` in the scene: each native does nothing and warns once; a dead `relativeTo` entity does nothing and warns once) moves onto `NativeCallHarness` with a `DebugDrawScripting` state the test constructs; `Items.cpp` and `Shapes.cpp` stay on .NET. `Tests/CMakeLists.txt` adds FenrirScripting's `src` include.

**Blocked by:** 01, 02

**Touches:** `packages/FenrirDebugDraw/Scripting/src/`, `packages/FenrirDebugDraw/Scripting/Tests/`

**Status:** ready-for-agent

- [ ] No facade-name string literal remains in `src/DebugDrawScripting.cpp` or the state header; the local span helper is gone.
- [ ] `NoOps` rule tests pass with no `FENRIR_TEST_DEBUGDRAW_FIXTURE` set; the remaining .NET tests pass with it.
- [ ] clang-format and the msvc-x64-debug preset pass.

## Comments

Filed from the native guard grilling (spec decisions 2, 4, 6, 12, 14). Runs side by side with tickets 03 to 06 once 01 and 02 are merged.
