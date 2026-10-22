# 09: The built-in extension on the same tooling

**What to build:** the engine's own scripting API described and generated like physics (spec: "Scope"; ADR-0009: the core and a package's extension are two users of one seam), so no hand mapping or hand-registered native remains anywhere.

- **API descriptions** for the packages that own the engine's shared types: `packages/FenrirECS/api/FenrirECS.api.toml` (`Name`, `Relationship` as components, `Relationship` read-only; `Entity` as a struct with a pointer member) and `packages/FenrirMath/api/FenrirMath.api.toml` (`Transform` as a component, `Vec3` and `Quat` as bound types), each with `fenrir_describe_api` in its package. If `Transform` lives elsewhere, the description goes with the package that defines it.
- **Binding configuration** `packages/FenrirScripting/api/FenrirScripting.binding.toml`: namespace `Fenrir.Scripting`, the renames `pos` → `Position`, `rot` → `Rotation`, `scale` → `Scale`, `Entity` → `EntityLink` with its fields `value`/`Value`, `reserved`, `owner`/`Owner`; `Vec3`/`Quat` bound to `System.Numerics`; natives from `src/Bindings.cpp`.
- **Generated**: `Fenrir.Scripting`'s `Transform`, `Name`, `Relationship` and `EntityLink` mirrors as partials (hand-written parts keep their behaviour: `EntityLink`'s helpers, `Name`'s text accessors); the Iris mappings replacing `EngineMappings.hpp` (`EntityLinkView` goes: `Entity` maps directly through the pointer kind of ticket 01); `EngineComponents()` replaced by the generated registration source; the 24 natives' C# declarations in the contract's `Native` class and their registration list generated from `Bindings.cpp` by the natives mode, so `BuiltInExtension`'s hand-written list goes.
- **Tests**: the generated defaults test for the engine components and the zeroing test for its natives join FenrirScripting's tests; hand-written cases they duplicate go.
- `packages/FenrirScripting/README.md` describes the built-in extension as generated and points at ADR-0016; `Fenrir.Scripting`'s public surface is diffed as physics's was, with no difference allowed.

**Blocked by:** 02 (state), 06 (generator), 08 (natives mode).

**Touches:** `packages/FenrirECS/api/`, `packages/FenrirECS/CMakeLists.txt`, `packages/FenrirMath/api/`, `packages/FenrirMath/CMakeLists.txt`, `packages/FenrirScripting/api/`, `packages/FenrirScripting/CMakeLists.txt`, `packages/FenrirScripting/include/FenrirScripting/EngineMappings.hpp`, `packages/FenrirScripting/src/Bindings.cpp`, `packages/FenrirScripting/src/`, `packages/FenrirScripting/Managed/Fenrir.Scripting/`, `packages/FenrirScripting/Tests/`, `packages/FenrirScripting/README.md`.

**Status:** ready-for-agent

- [ ] `grep -rn "IRIS_FIELD\|EntityLinkView\|NativeFunction::Make" packages/FenrirScripting` finds only generated files and the generator's own documentation.
- [ ] `Fenrir.Scripting.dll`'s public API diff is empty; the DemoApp's scripts, the physics fixture and every CompileFail case build unchanged.
- [ ] FenrirScripting's and FenrirPhysicsScripting's tests pass; the generated defaults test covers `Transform`, `Name`, `Relationship` and `EntityLink`.
- [ ] `fenrir-describe-api` then `fenrir-generate-bindings` leaves the tree clean across all three descriptions and both bindings.
