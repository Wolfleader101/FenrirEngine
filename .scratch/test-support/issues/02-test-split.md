# 02: `ScriptingTests.cpp` split by theme

**What to build:** the 5655-line test file becomes two helper headers and themed sources under 1000 lines each, with every case moved unchanged and the declaration order of dependent cases preserved (spec: "The test file split").

- **Helper headers** under `Tests/`: `TestExtension.hpp` holds the test-only scripting extension (the `TestNative` and `TestExtension` namespaces and whatever the anonymous namespace at line 630 declares that more than one theme uses), `FixtureCalls.hpp` the `CallIn` helpers and the fixture's type-name constants (`QueriesType` and its kin). Anything inline in a header that has a definition (not a template or `inline`) moves to a `TestExtension.cpp` listed in the executable.
- **Sources** named by theme, the case titles deciding membership: `HostingTests.cpp`, `ReloadTests.cpp`, `AdmissionTests.cpp`, `ScriptComponentTests.cpp`, `QueryTests.cpp`, `EventTests.cpp`, `ResourceTests.cpp`, `NativeTests.cpp`, `TaggedUnionTests.cpp`, adjusted to what the 115 titles group into; none over 1000 lines; each includes only what its cases use.
- **Order**: the test runs with `--order decl`. Before moving anything, find every case that depends on an earlier one (a shared `Scripting` install, a file the previous case wrote, a counter read across cases); each such pair goes into one file in the same relative order. List the pairs in the PR body. If a case depends on one in another theme, it follows the case it depends on, not its theme.
- **`Tests/CMakeLists.txt`**: `FenrirScriptingTests` lists the new sources and headers; `add_test` and its properties do not change.
- No case is renamed, re-tagged or edited beyond its includes and helpers.

**Blocked by:** 01

**Touches:** `packages/FenrirScripting/Tests/ScriptingTests.cpp`, `packages/FenrirScripting/Tests/*Tests.cpp`, `packages/FenrirScripting/Tests/TestExtension.*`, `packages/FenrirScripting/Tests/FixtureCalls.hpp`, `packages/FenrirScripting/Tests/CMakeLists.txt`

**Status:** resolved
- [x] `ScriptingTests.cpp` does not exist; no file under `packages/FenrirScripting/Tests/` is over 1000 lines (`wc -l`).
- [x] `FenrirScriptingTests --list-tests | wc -l` is 115 before and after; `ctest -R "^FenrirScripting$"` passes under the msvc-x64-debug preset, twice in a row.
- [x] `check_tickets.py` and clang-format pass.

## Comments

Filed from the test-support grilling (spec decision 4). Cut from `test-support-01-support-library`; ticket 03 is cut from this branch.
