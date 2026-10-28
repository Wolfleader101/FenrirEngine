# 03: the native call harness runs an extension and calls a native by its registered name

**What to build:** `NativeCallHarness` registers the extensions it is given as `Scripting::Install` does, is the active Scripting while it lives, calls a native through the registration's thunk by name, and owns the "warned once" assertion (spec: "The native call harness runs the extension as shipped").

- **`ScriptingExtension.hpp`**: `NativeFunction` gains two private members set by `Make`: `void* m_thunk` (`&Traits::Call`) and `std::type_index m_signature` (`typeid(R(Args...))` of the thunk's signature: context and state dropped), with private accessors; `NativeFunction` and `ScriptingExtension` add `friend class Testing::NativeCallHarness;` beside `friend class Scripting;`. **`NativeFunctionMake.hpp`**: both `Make` overloads pass the pointer and index to the constructor.
- **`WarnOnceRecord.hpp`**: `static std::string Line(std::string_view native, std::string_view message)` returns `<native>: <message> (warned once)`, and a one-argument `Line(message)` returns `<message> (warned once)`; `Warn(app, message)` logs `[<extension>] ` + `Line(message)` and `Warn(context, message)` calls it with `native + ": " + message`, so the suffix is spelled once. **`NativeGuard.hpp`**: `static constexpr std::string_view NotAlive = "the entity is not alive, so it does nothing";`, used by `Alive`.
- **`Tests/Support/NativeCallHarness.hpp/.cpp`**:
  - `explicit NativeCallHarness(std::vector<ScriptingExtension> extensions)` (and the default constructor, which registers nothing). The constructor, for each extension and each of its `functions`, calls `function.Register(extension.m_state.get())` and throws `std::runtime_error` naming the native and Iris's diagnostic if it fails; then activates `Impl`'s `Detail::EngineContext` (`app`, `components`, `changes` over the members `Impl` already has) with `Detail::ActivateEngineContext`, throwing if another context is active. The destructor deactivates it. The registered functions are kept (name, thunk, signature) for `Call`.
  - `template <typename R, typename... Args> R Call(std::string_view native, Args... arguments)`: finds the function whose `NativeNameFor(Name())` is `native` (throws naming it when none is registered), checks `std::type_index(typeid(R(Args...)))` against the recorded signature (throws naming the native and both `type_info::name()`s on a mismatch), and calls `reinterpret_cast<R (*)(Args...)>(thunk)(arguments...)`.
  - `static constexpr std::size_t once` (0 under `NDEBUG`, 1 otherwise); `std::size_t Warnings(std::string_view native, std::string_view message) const`, the number of `Warn`-level messages whose text ends with `WarnOnceRecord::Line(native, message)`; `bool WarnedOnce(native, message) const` is `Warnings(...) == once`. `Context`, `GetApp`, `Logger`, `ApplyChanges`, `PendingChanges` stay.
  - The header's comment describes the harness as the adapter at the registration seam (glossary: *native call harness*).
- **`Tests/Rules/NativeGuardTests.cpp`** (and `NativeMarshallingTests.cpp`, `ZeroingOutputsTests.cpp` if they define them): drop `once`, `Warnings`, `Logged` for `harness.Warnings` / `harness.once` / `NativeGuard::NotAlive`; a case that proves the extension prefix keeps one `Logger().Contains` on the whole line. A new case in `NativeGuardTests.cpp` or a new `Rules/NativeCallHarnessTests.cpp`: the harness given `TestExtension`'s extension (from `Tests/TestExtension.hpp`) calls one stateless and one stateful native by name and gets their results; a wrong result type and a wrong argument type each throw naming the native; an unregistered name throws; a native with an output is called with the output dirty and reads it zeroed when the native returns early (the registration's zeroing, through the thunk).
- **`packages/FenrirScripting/README.md`**: the harness paragraph says it runs extensions and calls by name.

**Blocked by:** 02

**Touches:** `packages/FenrirScripting/include/FenrirScripting/ScriptingExtension.hpp`, `packages/FenrirScripting/include/FenrirScripting/NativeFunctionMake.hpp`, `packages/FenrirScripting/include/FenrirScripting/WarnOnceRecord.hpp`, `packages/FenrirScripting/include/FenrirScripting/NativeGuard.hpp`, `packages/FenrirScripting/Tests/Support/NativeCallHarness.hpp`, `packages/FenrirScripting/Tests/Support/NativeCallHarness.cpp`, `packages/FenrirScripting/Tests/Rules/`, `packages/FenrirScripting/Tests/CMakeLists.txt`, `packages/FenrirScripting/README.md`

**Status:** ready-for-agent

- [ ] `grep -rn "warned once" packages/FenrirScripting --include=*.hpp --include=*.cpp` finds only `WarnOnceRecord.hpp`.
- [ ] The new harness cases pass; every `*Scripting*` CTest passes under the msvc-x64-debug preset (the six package rules tests still compile against the unchanged `Context`).
- [ ] `check_tickets.py` and clang-format pass.

## Comments

Filed from the rules-harness grilling (spec decisions 1, 2, 3, 5). Cut from ticket 02's branch; ticket 04 is cut from this one.
