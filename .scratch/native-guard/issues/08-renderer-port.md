# 08: Port the renderer extension onto NativeGuard, resolving model handles through Assets

**What to build:** Every renderer native becomes its one call behind the guard, and `SpawnModel` stops re-implementing Assets' handle rule (spec: "NativeGuard", decision 6).

- **`src/RendererScriptingState.hpp`**: `Alive(context, id, function, where)` goes in favour of `NativeGuard::Alive`; `CameraFor` loses its `std::string_view function` and `source_location` parameters and warns through a guard made from the context and `warnings`. The 8 hand-typed facade names go.
- **`src/RendererScripting.cpp`**: remove the `== nullptr` checks on `in` and `out` parameters (9 today) and the `*out = T{}` lines. `SpawnModel` (lines 162-175 today) replaces its own `TryGetResource<AssetStore>` and `IsValid` checks and their two warning texts with Assets' public `Resolve<Model>(guard, bits)` from ticket 05, so the words and the rule are Assets'. The parent check is `guard.Alive(parent)` when a parent is given.
- **Tests**: `Tests/NoOps.cpp` (no renderer installed: each native returns its default and warns once; `SpawnModel` with no store, a stale handle or a dead parent returns no entity and warns once) moves onto `NativeCallHarness` with a `RendererScripting` state the test constructs; `Drawing.cpp`, `Picking.cpp`, `Shapes.cpp` and `SpawnModel.cpp` stay on .NET. `Tests/CMakeLists.txt` adds FenrirScripting's `src` include.

**Blocked by:** 01, 02, 05

**Touches:** `packages/FenrirRenderer/Scripting/src/`, `packages/FenrirRenderer/Scripting/Tests/`

**Status:** ready-for-agent

- [ ] `src/RendererScripting.cpp` no longer names `AssetStore::IsValid` or carries the "no asset store" and "empty or stale" texts; `grep` for them finds only Assets.
- [ ] A rule test spawns through the harness with a stale model handle and sees Assets' warning, prefixed `[renderer scripting] RendererNative.SpawnModel: `.
- [ ] `NoOps` rule tests pass with no `FENRIR_TEST_RENDERER_FIXTURE` set; the remaining .NET tests pass with it.
- [ ] clang-format and the msvc-x64-debug preset pass.

## Comments

Filed from the native guard grilling (spec decisions 2, 4, 6, 14). Starts once tickets 01, 02 and 05 are merged.
