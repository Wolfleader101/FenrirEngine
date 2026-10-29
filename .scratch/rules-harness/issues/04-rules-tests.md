# 04: the six rules tests call through the registration, and every warning text is named on its state header

**What to build:** each package's rules test runs the package's own extension through the harness and names the package's warning texts as constants (spec: "The native call harness runs the extension as shipped", "The texts are named where the rules are").

- **The texts.** On each extension state header (`packages/<Pkg>/Scripting/src/<Pkg>ScriptingState.hpp`), every text the package's natives warn with becomes a `static constexpr std::string_view` named for its rule (`PhysicsScripting::NoWorld`, `NoBody`, `NoJoint`; `InputScripting::NoInput`, `NotConnected`; `WindowScripting::NoWindow`, `NoCursorMode`, `NoCursorShape`; `RendererScripting::NoRenderer`, `NoCamera`; `DebugDrawScripting::NoDebugDraw`; the Assets texts in `AssetsScripting.cpp`), the state's own lookups and the natives' sources using them; a text with a variable part becomes a static function returning the text (`PhysicsScripting::NotAliveFrames(bodyName)` for `" is not alive, so it gives identity frames"`). `packages/FenrirAssets/Scripting/include/FenrirAssetsScripting/AssetResolve.hpp` names `NoStore` and `StaleHandle(std::string_view kindName)`, used in place of its literals. The builder enumerates the eighteen texts by `grep -on '"[^"]*so it [a-z ]*"' packages/*/Scripting/src/` and names each; no text changes.
- **The six tests** (`FenrirPhysics/Scripting/Tests/Rules.cpp`, `FenrirWindow/.../Rules.cpp`, `FenrirInput/.../NoInput.cpp`, `FenrirAssets/.../NoOps.cpp`, `FenrirRenderer/.../NoOps.cpp`, `FenrirDebugDraw/.../NoOps.cpp`): construct `Testing::NativeCallHarness harness(<Pkg>::ScriptingExtension())` (window's rules take the window extension alone, as today's state does; renderer's the renderer's) in place of the harness and a state; every bare native call and every `ZeroingOutputs<...>::Call` becomes `harness.Call<R>("Type.Method", args...)` with the same arguments (a literal whose type is not the parameter's is spelled, `std::uint64_t{1}`, `"hello"sv`); every `Warnings(harness, Logged(native, Text)) == once` becomes `harness.WarnedOnce(native, <Pkg>Scripting::Text)`, a `== 0` becomes `harness.Warnings(native, text) == 0`; `once`, `Warnings`, `Logged` and the text constants are deleted; the includes of the natives header, the state header (unless a constant needs it) and `ZeroingOutputs.hpp` go. Assets' stale-handle checks call `AssetResolve::StaleHandle("mesh")` and so on. The renderer-reusing-Assets'-words case in `NoOps.cpp` (Assets, "SpawnModel") and the one in the renderer's keep their meaning with the constants. No case is renamed; what each asserts does not change.
- **`cmake/FenrirScriptingExtension.cmake`**: `fenrir_scripting_rules_tests`' comment says the natives are called through the registration by name with the package's extension; the `../src` include stays (the state header's constants).

**Blocked by:** 03

**Touches:** `packages/FenrirPhysics/Scripting/Tests/Rules.cpp`, `packages/FenrirWindow/Scripting/Tests/Rules.cpp`, `packages/FenrirInput/Scripting/Tests/NoInput.cpp`, `packages/FenrirAssets/Scripting/Tests/NoOps.cpp`, `packages/FenrirRenderer/Scripting/Tests/NoOps.cpp`, `packages/FenrirDebugDraw/Scripting/Tests/NoOps.cpp`, `packages/*/Scripting/src/*ScriptingState.hpp`, `packages/*/Scripting/src/*Scripting.cpp`, `packages/FenrirAssets/Scripting/include/FenrirAssetsScripting/AssetResolve.hpp`, `packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake`

**Status:** claimed

- [x] `grep -rn "ZeroingOutputs<\|harness.Context(\|warned once\|constexpr std::size_t once\|Generated/.*Natives.hpp" packages/*/Scripting/Tests` finds nothing.
- [x] `grep -rn 'so it [a-z ]*"' packages/*/Scripting/src/*.cpp` finds nothing: every text is on a state header or `AssetResolve.hpp`.
- [x] `ctest -N` lists the same `*ScriptingRules` tests; all pass under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Filed from the rules-harness grilling (spec decisions 1 to 4). Cut from ticket 03's branch.
