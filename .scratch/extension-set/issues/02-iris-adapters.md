# 02: the four Iris adapters gathered in IrisAdapters.{hpp,cpp}

**What to build:** a pure move: `IrisScriptAssemblyLoader`, `IrisSystemHostLoader` and `IrisNativeRegistrar` leave `Scripting.cpp`, `IrisScriptAssembly` leaves `ScriptData.{hpp,cpp}`, and all four live in one file, so "what a generation is to Iris" is read in one place and `Scripting.cpp` is left with `State`, `Scripting`'s members and Install (spec: "The Iris adapters in one file").

- **`packages/FenrirScripting/src/IrisAdapters.hpp`** (new): `Fenrir::Detail`, the declarations of the four adapters with their class comments as they are today: `IrisScriptAssemblyLoader` (constructor over `Iris::Host&`, `Load`, the static `Of`, its private `Loaded`), `IrisSystemHostLoader` (`Find`, its private `Host`), `IrisNativeRegistrar` (`Register`), and `IrisScriptAssembly` (constructor over `EngineContext&` and `const Iris::Component&`, `Report`, `Fingerprint`, `CheckNativeAgainstCSharp`). A header comment says these are the adapters at FenrirScripting's four seams over Iris (`ScriptAssemblyLoader`, `SystemHostLoader`, `NativeRegistrar`, `ScriptAssembly`), and that each fake under `Tests/Rules/` is the other adapter.
- **`packages/FenrirScripting/src/IrisAdapters.cpp`** (new): the definitions, moved verbatim: the loaders' and registrar's bodies from `Scripting.cpp`, and from `ScriptData.cpp` `IrisScriptAssembly`'s three methods with the two helpers only it uses, `ValidateTable` and `FingerprintOf`, in an anonymous namespace, with the includes they need.
- **`packages/FenrirScripting/src/Scripting.cpp`**: includes `IrisAdapters.hpp`; the anonymous namespace holding the three adapters goes; `Scripting::LoadedComponent` and the reconcile hook keep calling `Detail::IrisScriptAssemblyLoader::Of`. Includes no longer needed go (`SystemHostNames.hpp` among them if only the host loader used it).
- **`packages/FenrirScripting/src/ScriptData.hpp`**: `IrisScriptAssembly`'s declaration goes; the `ScriptAssembly` seam's comment, which names two adapters, names `IrisAdapters.hpp` as where the Iris one lives.
- **`packages/FenrirScripting/src/ScriptData.cpp`**: `IrisScriptAssembly`'s definitions, `ValidateTable` and `FingerprintOf` go, with the includes only they used.
- **`packages/FenrirScripting/CMakeLists.txt`**: `src/IrisAdapters.cpp` added.

No behaviour changes; no test changes. The `friend class Detail::IrisNativeRegistrar` line from ticket 01 already names the class in `Fenrir::Detail`, so the move needs no header change.

**Blocked by:** 01

**Touches:** `packages/FenrirScripting/src/IrisAdapters.hpp`, `packages/FenrirScripting/src/IrisAdapters.cpp`, `packages/FenrirScripting/src/Scripting.cpp`, `packages/FenrirScripting/src/ScriptData.hpp`, `packages/FenrirScripting/src/ScriptData.cpp`, `packages/FenrirScripting/CMakeLists.txt`

**Status:** resolved

Resolved: PR #226 merged into ticket 01's branch, #222 into the spec's, and the spec #217 into `dotnet-scripting` as `134c4fcf`.

- [x] Cut `extension-set-02-iris-adapters` from 01's pushed branch, targeting it.
- [x] `grep -n "class Iris" packages/FenrirScripting/src/*.hpp packages/FenrirScripting/src/*.cpp` finds the four adapters in `IrisAdapters.hpp` only; `grep -n "IrisScriptAssembly" packages/FenrirScripting/src/ScriptData.hpp packages/FenrirScripting/src/ScriptData.cpp` finds nothing.
- [x] The msvc-x64-debug preset builds; `FenrirScriptingRules` and `FenrirScripting` pass unchanged.

## Comments

Filed from the extension set grilling (spec decisions 5, 13 and 14).
