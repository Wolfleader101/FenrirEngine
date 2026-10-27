# 01: The generated natives header

**What to build:** Iris.Bindings writes each binding's native declarations into a header the library's own files include, and the 83 hand-written copies in the rules tests go (spec: "A generated natives header").

- **`BindingConfig.cs`**: `[natives]` gains `header` (a path relative to the configuration, required when `[natives]` is present) and `includes` (the list `[registration] includes` holds today, which is removed; a configuration still naming it fails with "moved to [natives] includes").
- **`CppWriter.cs`**: a new `NativesHeader(binding, natives, config)` writes `// clang-format off`, the generated-by banner, `#pragma once`, `#include "FenrirScripting/ScriptingExtension.hpp"`, each `[natives] includes` entry, the declarations `Declarations` writes today (in their namespaces), and the registration function's declaration. `Registration` no longer writes declarations or the function's own declaration: it includes the header by file name (both sit in the same `Generated/` directory) and defines the function. `Generator.cs` adds the header to the files it writes.
- **Seven configurations**: `[natives] header = "../src/Generated/Fenrir<Pkg>Natives.hpp"` and the moved `includes` in `FenrirAssets`, `FenrirDebugDraw`, `FenrirInput`, `FenrirPhysics`, `FenrirRenderer`, `FenrirWindow` and `FenrirScripting` `.binding.toml`.
- **Regenerate** with `fenrir-generate-bindings`: seven new headers, seven registration `.cpp` files that include them. `fenrir-describe-api` is unchanged (its natives mode reads definitions only).
- **The natives' sources** include their header: `src/<Pkg>Scripting.cpp` in each package (`#include "Generated/Fenrir<Pkg>Natives.hpp"`), and `src/Bindings.cpp`, which drops its hand-written `void RegisterScripting(ScriptingExtension&);` declaration.
- **The rules tests** (`FenrirAssets/Scripting/Tests/NoOps.cpp`, `FenrirDebugDraw/.../NoOps.cpp`, `FenrirInput/.../NoInput.cpp`, `FenrirPhysics/.../Rules.cpp`, `FenrirRenderer/.../NoOps.cpp`, `FenrirWindow/.../Rules.cpp`) include the header through their existing `../src` include directory and delete their declaration blocks and the "no header the engine sees declares them" comments.
- **The library sources** in each `Scripting/CMakeLists.txt` list `src/Generated/Fenrir<Pkg>Natives.hpp` (ticket 02 moves this into the module).
- **README** (`packages/Iris/Tools/Iris.Bindings/README.md`): the configuration listing shows `[natives] header` and `includes`; "What it writes" gains the header's row and the registration row no longer says it declares the natives.

**Blocked by:** none

**Touches:** `packages/Iris/Tools/Iris.Bindings/src/`, `packages/Iris/Tools/Iris.Bindings/Tests/`, `packages/Iris/Tools/Iris.Bindings/README.md`, `packages/*/Scripting/api/*.binding.toml`, `packages/*/Scripting/src/`, `packages/*/Scripting/Tests/*.cpp`, `packages/*/Scripting/CMakeLists.txt`, `packages/FenrirScripting/api/FenrirScripting.binding.toml`, `packages/FenrirScripting/src/`

**Status:** claimed

- [x] `BindingTests.cs`: a case reads the fixture binding's generated `FixtureNatives.hpp` and finds each native's declaration and the registration function's declaration, and finds no declaration in `FixtureRegistration.cpp`; a case fails a configuration that keeps `[registration] includes`.
- [x] `grep -rn "const CallContext&" packages/*/Scripting/Tests/*.cpp` finds nothing; `grep -n "Generated::" packages/FenrirScripting/src/Bindings.cpp` finds only the call.
- [x] `fenrir-generate-bindings` is a no-op after the commit; `check_tickets.py`, clang-format and the msvc-x64-debug preset build and test.

## Comments

Filed from the extension-shape grilling (spec decisions 2, 3, 8, 9). Cut from `dotnet-scripting`; tickets 02 and 03 are cut from this branch in that order.
