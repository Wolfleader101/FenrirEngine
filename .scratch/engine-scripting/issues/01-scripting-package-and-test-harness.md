# 01: FenrirScripting package and headless test harness

**What to build:** A new package `packages/FenrirScripting`, added to the build only when `FENRIR_ENABLE_SCRIPTING` is on. `Scripting::Install(App&, ScriptingConfig)` starts the Iris `Host` (runtime config, bridge, `dotnetRoot`, shared assemblies), routes the Iris log sink to the engine `ILogger`, and loads the project's one script assembly, whose path comes from a new `.feproj` field. All Iris owners are destroyed before `main` returns. This ticket also adds the headless test target that every later ticket proves itself in: an `App` with no window that runs N frames, plus a fixture script assembly built by CMake.

**Blocked by:** none

**Status:** resolved

- [x] Building with scripting OFF neither configures nor invokes `dotnet`, and doesn't compile FenrirScripting.
- [x] With scripting ON, the test target loads the fixture assembly and runs frames. Iris log messages appear through `ILogger`.
- [x] A missing or invalid script assembly is logged and the app keeps running without scripts. It does not crash.
- [x] Shutdown releases everything Iris owns before process exit (no runtime calls after `main`).
- [x] `iris_deploy` (or its equivalent) places nethost and the bridge next to the test executable.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Build).

### Resolution

- **Package:** `packages/FenrirScripting` (static library, README, tests), added from the root `CMakeLists.txt` only inside `if(FENRIR_ENABLE_SCRIPTING)`. `enable_testing()` is called there too.
- **API:** `Scripting::Install(App&, ScriptingConfig)` returns a `Scripting` owner (movable, not copyable). It starts the Iris `Host`, forwards Iris's log sink to `app.Logger()` as `[Iris] ...` (the text is passed as a format argument, never as the format string, since Iris messages can contain braces), and loads the script assembly. The owner also has `IsRunning()`, `HasScripts()` and an idempotent `Shutdown()`.
- **Failure is never fatal:** an empty path does nothing and never starts .NET; a missing file, an unstartable runtime or bridge, and an invalid assembly are each logged as an error, and the app keeps running.
- **Shutdown:** Install registers a *sequential* `Exit`-phase system (so it runs on the main thread, not the thread pool) that releases the component, clears the sink and destroys the host. The owner's destructor does the same. A test checks that nothing of Iris is left once `Run` returns.
- **`.feproj`:** `ProjectSettings` (DemoApp) has a new `scriptAssembly` field, read through the existing glaze meta. DemoApp calls `Install` when built with scripting (`FENRIR_SCRIPTING`), links `FenrirScripting` and calls `iris_deploy`. The existing `demoApp.feproj` is unchanged: a missing field means no scripts.
- **Headless harness:** `Tests/HeadlessApp.hpp` (`HeadlessApp` plus a `RecordingLogger`), with a fixture script assembly (`Tests/Fixture`) built by CMake with the SDK Iris found. Seven cases in `FenrirScriptingTests` cover each criterion; it passes in Debug and Release.
- **Iris changes (additive build glue):** `Iris::Nethost` is now a GLOBAL imported target, and a parent project that adds Iris as a subdirectory gets `IRIS_BRIDGE_DIR` in its scope, so a sibling package can call `iris_deploy`.
- **OFF builds:** a fresh configure with scripting OFF has no `dotnet` in its cache and no Iris or FenrirScripting target. DemoApp builds with it OFF and has no Iris files beside it.
- **Pre-existing, unrelated:** the first configure of a fresh build directory fails once in the third-party assimp project and succeeds on the second run (reproduced with these changes stashed).
