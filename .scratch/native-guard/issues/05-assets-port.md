# 05: Port the assets extension onto NativeGuard, and publish its handle resolve for other extensions

**What to build:** Every assets native becomes its one call behind the guard, and the "no store, then stale handle" rule becomes something another extension calls rather than copies (spec: "NativeGuard", decision 6).

- **State moves to the public header**: `src/AssetsScriptingState.hpp`'s `StoreFor`, `Resolve<T>`, `StoreAndOwnerFor`, `OwnerOf` and `Placed` lose their `std::string_view function` and `source_location` parameters and warn through a `NativeGuard` made from the context and `warnings`. `StoreFor(context)` and `Resolve<T>(context, bits)` move to `include/FenrirAssetsScripting/AssetsScripting.hpp` (or a new `AssetResolve.hpp` there) as free functions taking the guard, so Renderer (ticket 08) resolves a model handle with Assets' rule and Assets' words. The extension state keeps its record and anything only Assets uses.
- **`src/AssetsScripting.cpp`**: remove the `== nullptr` checks on `in` and `out` parameters (26 today) and the `*out = T{}` lines. The twelve `ReadInto` calls lose their name argument. The span copy with its clang pragma (lines 36-56 today) is replaced by `SpanOf`; `GetShaderParameter`'s ASCII-only text-out by `WriteText`. The 36 hand-typed facade names go. The 22 `AssetHandleFromBits` calls stay: ADR-0044 keeps the handle crossing as one packed 64-bit number.
- **Tests**: `Tests/NoOps.cpp` (no asset store: each native returns its default and warns once; a stale or empty handle does nothing and warns once) moves onto `NativeCallHarness` with an `AssetsScripting` state the test constructs; `Images.cpp`, `Loading.cpp`, `Materials.cpp`, `Meshes.cpp`, `Shaders.cpp` and `Shapes.cpp` stay on .NET. `Tests/CMakeLists.txt` adds FenrirScripting's `src` include.

**Blocked by:** 01, 02

**Touches:** `packages/FenrirAssets/Scripting/src/`, `packages/FenrirAssets/Scripting/include/FenrirAssetsScripting/`, `packages/FenrirAssets/Scripting/Tests/`

**Status:** resolved

- [x] No facade-name string literal remains in `src/AssetsScripting.cpp` or the state header; the span copy and the hand-rolled text-out are gone.
- [x] A test in `NoOps.cpp` calls the public `Resolve<Model>` through the harness with no store, then with a stale handle, and sees one warning each, with the words Renderer will reuse.
- [x] `NoOps` rule tests pass with no `FENRIR_TEST_ASSETS_FIXTURE` set; the remaining .NET tests pass with it.
- [x] clang-format and the msvc-x64-debug preset pass.

## Comments

Filed from the native guard grilling (spec decisions 2, 4, 6, 12, 14). Runs side by side with tickets 03, 04, 06 and 07 once 01 and 02 are merged; ticket 08 waits for this one.
