# FenrirScripting

C# scripting for FenrirEngine, built on [Iris](../Iris/README.md). The design is in
[ADR-0007](../../docs/adr/0007-ecs-first-csharp-scripting.md) and `.scratch/engine-scripting/spec.md`.

The package exists only when the root build is configured with `-DFENRIR_ENABLE_SCRIPTING=ON`. With it OFF,
CMake never looks for .NET, never adds Iris or this package, and never runs `dotnet`.

## Using it

```cpp
Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
// Declared after the app: .NET is released before the app is destroyed.
auto scripting = Fenrir::Scripting::Install(app, Fenrir::ScriptingConfig{.scriptAssembly = "scripts/MyGame.dll"});
app.Run();
```

`Scripting::Install` starts the Iris `Host`, forwards Iris's log messages to `app.Logger()` (tagged `[Iris]`), and
loads the one script assembly. It never throws, and a missing or invalid assembly, or a missing runtime, is
logged as an error and the app runs without scripts (`HasScripts()` is false).

- **Where .NET comes from.** The Iris bridge (`Iris.Bridge.dll`, its `.runtimeconfig.json`) and `nethost` must sit
  next to the executable, or in `ScriptingConfig::bridgeDirectory`. In CMake, call `iris_deploy(<target>)` on the
  executable.
- **The script assembly path** comes from the project: the `scriptAssembly` field of the `.feproj` (DemoApp reads it
  into `ProjectSettings`). Empty means no scripts, and .NET is not started at all.
- **Shutdown.** Everything Iris owns is released when the app finishes `Run` (a sequential system in the `Exit`
  phase, on the main thread), and again, harmlessly, when the `Scripting` object is destroyed. Destroy it before
  `main` returns.

## Tests

`FenrirScriptingTests` runs an `App` with no window for a few frames around a small fixture script assembly
(`Tests/Fixture`), built with the same .NET SDK Iris uses. Run it with CTest:

```sh
cmake -S . -B out/build/engine-scripting -DFENRIR_ENABLE_SCRIPTING=ON
cmake --build out/build/engine-scripting --config Debug --target FenrirScriptingTests
ctest --test-dir out/build/engine-scripting -C Debug --output-on-failure
```

`Tests/HeadlessApp.hpp` is the harness later tickets build on: `HeadlessApp` owns an `App` and a recording
logger, and `Run(n)` runs exactly `n` frames.

The first configure of a fresh build directory can fail in the third-party assimp project; running the same
configure command again succeeds. That happens with scripting OFF too.
