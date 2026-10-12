# 01: FenrirScripting package and headless test harness

**What to build:** A new package `packages/FenrirScripting`, added to the build only when `FENRIR_ENABLE_SCRIPTING` is on. `Scripting::Install(App&, ScriptingConfig)` starts the Iris `Host` (runtime config, bridge, `dotnetRoot`, shared assemblies), routes the Iris log sink to the engine `ILogger`, and loads the project's one script assembly, whose path comes from a new `.feproj` field. All Iris owners are destroyed before `main` returns. This ticket also adds the headless test target that every later ticket proves itself in: an `App` with no window that runs N frames, plus a fixture script assembly built by CMake.

**Blocked by:** none

**Status:** ready-for-agent

- [ ] Building with scripting OFF neither configures nor invokes `dotnet`, and doesn't compile FenrirScripting.
- [ ] With scripting ON, the test target loads the fixture assembly and runs frames. Iris log messages appear through `ILogger`.
- [ ] A missing or invalid script assembly is logged and the app keeps running without scripts. It does not crash.
- [ ] Shutdown releases everything Iris owns before process exit (no runtime calls after `main`).
- [ ] `iris_deploy` (or its equivalent) places nethost and the bridge next to the test executable.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Build).
