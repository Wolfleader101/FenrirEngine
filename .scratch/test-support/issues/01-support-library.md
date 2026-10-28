# 01: `FenrirScriptingTestSupport`: the shared test headers and the harness behind its interface

**What to build:** one library owns the four shared test headers and the harness's implementation, so no test executable outside FenrirScripting is given FenrirScripting's `src` (spec: "`FenrirScriptingTestSupport`").

- **`packages/FenrirScripting/Tests/Support/CMakeLists.txt`** (new, added from `Tests/CMakeLists.txt` before the executables): `add_library(FenrirScriptingTestSupport STATIC NativeCallHarness.cpp <the four headers>)`, `target_include_directories(... PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")`, `target_link_libraries(... PUBLIC FenrirScripting)`, plus Catch2 only if a header needs it (today none does: check and say). The MSVC `_CRT_SECURE_NO_WARNINGS` definition for `std::getenv` moves here as `PUBLIC` if the headless app or probe reads the environment, otherwise stays where it is.
- **Move** `HeadlessApp.hpp`, `ScriptingProbe.hpp`, `ExtensionTestApp.hpp`, `NativeCallHarness.hpp` from `Tests/` to `Tests/Support/` (`git mv`). `NativeCallHarness.hpp` keeps its public interface and doc, drops the private includes and the paragraph about `src`, and holds `struct ApplyOutcome { std::size_t applied = 0; std::size_t skipped = 0; };` and `class NativeCallHarness` with the five methods declared, a constructor and destructor declared, copies deleted, and `std::unique_ptr<Impl> m_impl`. **`NativeCallHarness.cpp`** defines `Impl` (the headless app, the empty registry, the change buffer) and the methods; `ApplyChanges` converts `Detail::ApplyResult` to `ApplyOutcome`.
- **`cmake/FenrirScriptingExtension.cmake`**: `fenrir_scripting_tests` and `fenrir_scripting_rules_tests` link `FenrirScriptingTestSupport`, drop the header paths from their source lists and `${tests_dir}` and `${tests_dir}/../src` from their include directories (the rules function keeps `${CMAKE_CURRENT_SOURCE_DIR}/../src`); `tests_dir` goes if nothing else reads it. The module's header comment stops describing the leak.
- **`Tests/CMakeLists.txt`**: both executables link the library and drop the four headers from their source lists; `FenrirScriptingTests` drops `../src` from its includes if only the harness needed it (check: `ScriptingTests.cpp` includes `NativeCallHarness.hpp` and `ScriptingProbe.hpp`, nothing private, but verify by building); `FenrirScriptingRulesTests` keeps `../src`. Comments about "every test executable using the harness is given the source directory" go.
- **Callers**: `Tests/Rules/NativeGuardTests.cpp` and `ZeroingOutputsTests.cpp` include `"NativeCallHarness.hpp"` instead of `"../NativeCallHarness.hpp"`; any test reading `.applied`/`.skipped` from the harness compiles unchanged against `ApplyOutcome`.

**Blocked by:** none

**Touches:** `packages/FenrirScripting/Tests/Support/`, `packages/FenrirScripting/Tests/CMakeLists.txt`, `packages/FenrirScripting/Tests/*.hpp`, `packages/FenrirScripting/Tests/Rules/NativeGuardTests.cpp`, `packages/FenrirScripting/Tests/Rules/ZeroingOutputsTests.cpp`, `packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake`

**Status:** resolved
- [x] `grep -rn "ChangeBuffer.hpp\|ComponentRegistry.hpp\|ApplyResult" packages/FenrirScripting/Tests/Support/*.hpp` finds nothing; `grep -n '/../src' packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake` finds only the package's own `src` line.
- [x] Every scripting test target configures and builds under the msvc-x64-debug preset; `ctest -R "Scripting"` passes with the same test names as before.
- [x] `check_tickets.py` and clang-format pass.

## Comments

Filed from the test-support grilling (spec decisions 2, 3). Cut from `dotnet-scripting`; ticket 02 is cut from this branch.
