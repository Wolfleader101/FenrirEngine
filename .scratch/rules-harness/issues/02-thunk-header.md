# 02: `NativeFunctionMake.hpp`: the thunks and `Make`'s bodies leave the public header

**What to build:** `ScriptingExtension.hpp` keeps what every includer of a package's extension needs; what only a registration needs moves to a header the registrations include (spec: "The thunks leave the public header").

- **`packages/FenrirScripting/include/FenrirScripting/NativeFunctionMake.hpp`** (new): `Detail::ActiveScripting` and `ActiveCallContext()`, `Detail::NativeFunctionCall`, the four `NativeFunctionTraits` specialisations, `Detail::StateTypeOf`, and the out-of-class definitions of both `NativeFunction::Make` overloads (`template <auto Function> NativeFunction NativeFunction::Make(std::string name) { ... }`), moved verbatim. It includes `ScriptingExtension.hpp` and `<Iris/Iris.hpp>` (for `Iris::RegisterInternalCall`). Its header comment says who includes it: the generated registrations through the defaults file, and a hand-written maker by hand.
- **`ScriptingExtension.hpp`**: keeps the forward declarations (`CallContext`, `Scripting`, `Testing::NativeCallHarness`, `Detail::ChangeBuffer`, the `NativeFunctionCall` template declaration the friend line needs), `ChangeBuffer`, `CallContext`, `NativeFunction` with `Make` declared only (its doc comment stays on the declaration), `ScriptingExtension`. `#include <Iris/Iris.hpp>` becomes `<Iris/CallResult.hpp>` (the registrar's return type); drop any other include only the moved code used. The doc on `NativeFunction` points at `NativeFunctionMake.hpp` for the definition.
- **`packages/FenrirScripting/api/FenrirScripting.binding-defaults.toml`**: `[natives] includes` gains `"FenrirScripting/NativeFunctionMake.hpp"` (the SDK's headers come first; order it after `ScriptingExtension.hpp`), with its comment extended. Regenerate the seven natives headers (`fenrir-generate-bindings`); each gains the include line and nothing else.
- **Hand-written makers**: `src/Bindings.cpp`, `Tests/TestExtension.cpp`, `Tests/AdmissionTests.cpp` and `Tests/Rules/ZeroingOutputsTests.cpp` include the new header. Anything else that fails to compile because it reached `Iris/Iris.hpp` through `ScriptingExtension.hpp` includes what it uses.
- **README**: `packages/FenrirScripting/README.md`'s description of the registration thunk names the new header.

**Blocked by:** none

**Touches:** `packages/FenrirScripting/include/FenrirScripting/ScriptingExtension.hpp`, `packages/FenrirScripting/include/FenrirScripting/NativeFunctionMake.hpp`, `packages/FenrirScripting/api/FenrirScripting.binding-defaults.toml`, `packages/FenrirScripting/src/Bindings.cpp`, `packages/FenrirScripting/Tests/TestExtension.cpp`, `packages/FenrirScripting/Tests/AdmissionTests.cpp`, `packages/FenrirScripting/Tests/Rules/ZeroingOutputsTests.cpp`, `packages/FenrirScripting/README.md`, `packages/*/Scripting/src/Generated/*Natives.hpp`, `packages/FenrirScripting/src/Generated/FenrirScriptingNatives.hpp`

**Status:** resolved

- [x] `grep -n "Iris/Iris.hpp\|NativeFunctionCall\b.*{\|NativeFunctionTraits\|StateTypeOf" packages/FenrirScripting/include/FenrirScripting/ScriptingExtension.hpp` finds nothing but the friend's forward declaration.
- [x] Every generated natives header includes `FenrirScripting/NativeFunctionMake.hpp`; `fenrir-generate-bindings` re-run changes nothing further.
- [x] Every `*Scripting*` CTest passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Filed from the rules-harness grilling (spec decision 10). Cut from `dotnet-scripting`; ticket 03 is cut from this branch.

Resolved: PR #224 merged into the spec's branch, and the spec #218 squash-merged into `dotnet-scripting` carrying it.
