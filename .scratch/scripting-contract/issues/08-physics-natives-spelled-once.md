# 08: Physics natives spelled once

**What to build:** the native-function half of candidates E and H (spec: "Native functions spelled once"): a native is written once, as its C++ function; its C# declaration and its registration are generated.

- **`tools/describe-api --natives`**: a second mode reading an extension's C++ sources (configured in a `natives` section of the binding configuration, since natives are binding glue, not the neutral description) and writing `packages/<Package>/Scripting/api/<Package>.natives.json`: every free function whose first parameter is `const CallContext&`, with its name, whether its second parameter is the extension's state, and each remaining parameter's C++ type and name, plus the return type and the function's doc comment. Functions are written in source order of one configured list of files, so the registration order is deterministic.
- **The Iris tool** reads it and emits: the C# `static partial class <Native>` (`PhysicsNative`) declarations, `[NativeFunction] public static partial R Name(...)` with the leading context and state dropped and the C++ parameter types mapped as the hand-written declarations are today (`std::uint32_t entity` → `Entity`, `const T*` → `in T`, `T*` output → `out T` when the C++ doc marks it an output and `T*` plus a count otherwise, `std::int32_t`/`float`/`bool`/`std::uint64_t` as themselves, `const char*` + length → `string`); and the C++ `extension.functions = { NativeFunction::Make<&F>("Namespace.Native.F"), ... }` list in the registration source. A C++ type it cannot map fails naming the function and parameter.
- **FenrirPhysicsScripting**: `CreateMesh` → `CreateCollisionMesh`, `ReleaseMesh` → `ReleaseCollisionMesh`, `FrameFromWorldNative` → `FrameFromWorld` (in a namespace that does not clash with FenrirPhysics's free function), so every native is named as C# calls it; the hand-written `extension.functions` list goes; `PhysicsNative`'s 23 hand-written declarations go from `PhysicsWorld.cs`, which keeps the friendly wrappers, now without the nine `filter ?? new QueryFilter()` lines (a `QueryFilter` default parameter of `default` and a `QueryFilter` whose `default` means "everything").
- **Test**: the natives test from ticket 03 (every output zeroed with no world) is generated from the natives file, one case per native with an output.
- **Build and CI**: the natives mode runs under `fenrir-describe-api`; the clean-tree check covers the natives file.

**Blocked by:** 07 (generated contract and registration source), 04, 06.

**Touches:** `tools/describe-api/`, `packages/Iris/Tools/`, `packages/FenrirPhysics/Scripting/api/`, `packages/FenrirPhysics/Scripting/src/`, `packages/FenrirPhysics/Scripting/Tests/`, `packages/FenrirPhysics/Managed/Fenrir.Physics/PhysicsWorld.cs`, `packages/FenrirPhysics/README.md`.

**Status:** ready-for-agent

- [ ] `grep -rn "NativeFunction::Make" packages/FenrirPhysics/Scripting/src` finds only the generated registration source; `grep -n "\[NativeFunction\]" packages/FenrirPhysics/Managed` finds only `Generated/`.
- [ ] Each of the 23 natives' names appears exactly once in hand-written code (its C++ definition); the DemoApp's C# scripts and the test fixture build unchanged against the regenerated `PhysicsNative`.
- [ ] A native added to the C++ with a new output parameter appears in the generated declaration and in the zeroing test after regeneration (proved once in the PR, then removed).
- [ ] `FenrirPhysicsScriptingTests` passes; the tool's fixture tests cover the natives mode, including the failing case of an unmappable parameter type.
