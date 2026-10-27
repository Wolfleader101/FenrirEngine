---
status: accepted
---

# A scripting extension's shape is generated and shared: a package writes its natives and its state

By the second review of the scripting branch (2026-10-27), six packages carried a scripting extension, and each spelled the extension's shape by hand where one owner could. Iris.Bindings wrote each native's declaration into the registration `.cpp`, a file nothing includes, so the natives' own source was checked against the committed description only at link and five rules tests copied the declarations by hand. Each package's extension-making function repeated the same four statements, two of them literals the generator already knew. Each package's two CMakeLists were the same two files with the name substituted, and every pipeline change touched all twelve.

We decided:

- **A package writes its native functions, its extension state and its hand-written registrations. Everything else about its extension's shape is generated or shared.** The declarations, the extension maker and the build are not a package's to spell.
- **Iris.Bindings writes a natives header.** `[natives] header` names a private header (`src/Generated/<Pkg>Natives.hpp`) holding each native's declaration and the maker's. The registration `.cpp`, the natives' source and the rules tests include it; nothing declares a native by hand. The natives' source including its own generated declarations is the point: a definition that drifts from the committed description fails in the library's compile.
- **Iris.Bindings writes the extension maker.** `[registration] extension` names a function taking the extension state and returning the filled `ScriptingExtension`: the binding's name, the state, the contract assembly named from the namespace, and the registration lists. A package's public `<Area>::ScriptingExtension()` calls it and appends only what `by-hand` leaves to it. The built-in extension, which is Install's own, has no maker.
- **One CMake module owns the build.** `FenrirScriptingExtension.cmake` derives the contract, library, fixture and two test executables from the package name; a package passes a keyword only where it deviates (`USES`, `NO_SELECTION`, `DESCRIPTIONS`, `TEST_ARGS`). `fenrir_deploy_scripting(<target> EXTENSIONS ...)` deploys any extension's contract.

## Considered options

- **Keep the declarations in the registration `.cpp` and the copies in the tests.** Rejected: the generator already has the declarations; the only reason they were copied is that it wrote them where nothing could include them.
- **A public natives header.** Rejected: the natives take the extension state, a private type, and the header would publish it. The rules tests already see the package's `src`.
- **Fold `name` and `sharedAssemblies` into the register function and keep the four-statement maker by hand.** Rejected as the halfway house: the state type is also known to the generator, and a maker taking the state leaves the hand-written function nothing but its exceptions.
- **Per-package CMake with shared snippets `include()`d.** Rejected: the twelve files were already one file with the name substituted, and a snippet cannot derive the names. Functions with keywords can.
- **FenrirScripting's own build on the module.** Rejected: its contract is the SDK, its fixture builds twice for the reload tests, and the built-in extension is not a package's. It is the one exception and stays explicit.

## Consequences

- `GLOSSARY.md`'s *scripting extension* entry says where an extension's shape comes from.
- Iris.Bindings gains `[natives] header` and `includes` (moved from `[registration] includes`) and `[registration] extension`; FenrirScripting gains `cmake/FenrirScriptingExtension.cmake`, and `fenrir_deploy_scripting` gains `EXTENSIONS`. The six per-package deploy functions go.
- A seventh package's extension is two CMakeLists of three calls each, a binding configuration, its natives and its state. Its public function is one line.
- The rules tests' include of FenrirScripting's `src` (NativeCallHarness's leak of private headers) is spelled once, in the module, until it is closed.
