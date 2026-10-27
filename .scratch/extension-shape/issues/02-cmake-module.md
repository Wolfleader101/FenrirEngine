# 02: One CMake module for a scripting extension's build

**What to build:** `FenrirScriptingExtension.cmake` owns the contract, library, fixture and test shape every package extension repeats, and the six packages' twelve CMakeLists become calls into it (spec: "One CMake module").

- **`packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake`**, included from `packages/FenrirScripting/CMakeLists.txt` after `FenrirScriptingDeploy.cmake`, with the five functions the spec lists and their keywords (`cmake_parse_arguments`): `fenrir_scripting_contract(<Pkg> [USES ...])`, `fenrir_scripting_extension(<Pkg> SOURCES ... LINK ... [NO_SELECTION] [DESCRIPTIONS ...])`, `fenrir_script_fixture(<Pkg> [USES ...])`, `fenrir_scripting_tests(<Pkg> SOURCES ... [TEST_ARGS ...])`, `fenrir_scripting_rules_tests(<Pkg> SOURCES ... [TEST_ARGS ...])`. Every path, target name, cache variable, csproj property (`Fenrir<Pkg>ContractDir`) and environment variable (`FENRIR_TEST_<PKG>_FIXTURE`) is derived from `<Pkg>`; `string(TOUPPER)` and `string(TOLOWER)` give the variable and directory spellings. The library function lists the three generated files itself and adds `Tests` under `FENRIR_BUILD_TESTS`. The contract function records `<Pkg>` to contract directory in a global property `FENRIR_SCRIPTING_CONTRACTS`. The header comment of the module carries what the twelve files' comments say today once: why a package's extension is a library of its own, where the contract goes and who copies it, what the rules tests are and why they see two `src` directories (the harness leak, to be closed by its own change).
- **`FenrirScriptingDeploy.cmake`**: `fenrir_deploy_scripting(<target> [EXTENSIONS <Pkg>...])` copies each named extension's contract from the property, failing when a name was never recorded. The six `fenrir_deploy_<pkg>_scripting` functions are deleted.
- **Twelve CMakeLists** rewritten as calls. Each `Scripting/CMakeLists.txt`: its first comment paragraph (what the package gives scripting, and when the root adds it), then `fenrir_scripting_contract`, `fenrir_scripting_extension`. Each `Tests/CMakeLists.txt`: `fenrir_scripting_rules_tests`, `fenrir_scripting_tests`, `fenrir_script_fixture`. Window: `NO_SELECTION`, `USES Input` on contract and fixture. DebugDraw: `DESCRIPTIONS api/FenrirDebugDrawScripting.api.json`. Renderer: `USES Input Assets` as its csproj files reference. Physics: `TEST_ARGS --order decl` on both test functions, its compile-fail function and cases kept after them. Any other per-package line found during the rewrite is kept as the package's own line below the calls and named in the PR.
- **Call sites**: `examples/PhysicsDemo/CMakeLists.txt` and `examples/DemoScaffold/Tests/CMakeLists.txt` pass `EXTENSIONS`; the hand-written headers' comments that name `fenrir_deploy_<pkg>_scripting` (`include/Fenrir<Pkg>Scripting/<Pkg>Scripting.hpp`) name the new form.
- **README** "Building and testing" if it names a deploy function.

**Blocked by:** 01

**Touches:** `packages/FenrirScripting/cmake/`, `packages/FenrirScripting/CMakeLists.txt`, `packages/*/Scripting/CMakeLists.txt`, `packages/*/Scripting/Tests/CMakeLists.txt`, `packages/*/Scripting/include/`, `examples/PhysicsDemo/CMakeLists.txt`, `examples/DemoScaffold/Tests/CMakeLists.txt`, `README.md`

**Status:** ready-for-agent

- [ ] The configured build has the same targets and tests as before (`ctest -N` before and after lists the same names); every scripting test passes under the msvc-x64-debug preset.
- [ ] `grep -rn "function(fenrir_deploy_" packages examples` finds only `fenrir_deploy_scripting`; no `Scripting/CMakeLists.txt` names `add_custom_command`, `add_library` or `add_executable`.
- [ ] `fenrir-describe-api` and `fenrir-generate-bindings` still cover every binding (the targets `fenrir-describe-api-*` and `iris-generate-binding-*` exist for each package) and are no-ops.
- [ ] `check_tickets.py` and clang-format pass.

## Comments

Filed from the extension-shape grilling (spec decisions 5, 6, 10 to 13). Cut from `extension-shape-01-natives-header`, since the library function lists the header that ticket adds.
