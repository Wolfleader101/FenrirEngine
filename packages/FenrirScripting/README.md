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

- **Where .NET comes from.** The Iris bridge (`Iris.Bridge.dll`, its `.runtimeconfig.json`), `nethost` and the
  scripting contract `Fenrir.Scripting.dll` must sit next to the executable, or in
  `ScriptingConfig::bridgeDirectory`. In CMake, call `fenrir_deploy_scripting(<target>)` on the executable.
- **The contract.** `Fenrir.Scripting` (`Managed/Fenrir.Scripting`) is the C# API scripts compile against: `Entity`,
  `Log` and `Time` so far. `Scripting::Install` always shares it with Iris, as one copy in the default load context
  that is never reloaded, so its types have one identity across every load and reload of the script assembly. A
  script project references it with `<Private>false</Private>` (compile against it, do not copy it). The version is
  part of the contract: a script built against a newer minor than the host's fails to load.
- **One active Scripting.** The engine's native functions act on the App of the `Scripting` that is active, so a
  second `Install` while one is alive logs an error and runs without scripts.
- **The script assembly path** comes from the project: the `scriptAssembly` field of the `.feproj` (DemoApp reads it
  into `ProjectSettings`). Empty means no scripts, and .NET is not started at all.
- **Shutdown.** Everything Iris owns is released when the app finishes `Run` (a sequential system in the `Exit`
  phase, on the main thread), and again, harmlessly, when the `Scripting` object is destroyed. Destroy it before
  `main` returns.

## Adding an engine API

Every engine API is the same three pieces, which keeps pointers, UTF-8 and lengths out of script code. `Log` is the
smallest complete example.

1. **A native function** in `src/Bindings.cpp`: a plain C++ function over C types (sized integers, `float`, `double`,
   `bool`, pointers). It reads the App through `Active()`. A C++ exception that escapes it is stopped by Iris and
   logged; scripts get a zero result.

   ```cpp
   void Log(std::int32_t level, const char* text, std::int32_t length) { /* ... */ }
   ```

2. **Its registration**, in `RegisterEngineBindings`, under the name `Fenrir.Scripting.Native.<Field>`:

   ```cpp
   Iris::RegisterInternalCall<&Log>(prefix + "Log"),
   ```

3. **A field in the contract's `Native` class** (`Managed/Fenrir.Scripting/Native.cs`) with exactly that signature, and a
   **friendly wrapper** beside it that scripts use:

   ```csharp
   public static delegate* unmanaged[Cdecl]<int, byte*, int, void> Log;     // in Native (internal)

   public static class Log { public static void Info(string? message) => Write(0, message); /* UTF-8, length, fixed */ }
   ```

Iris binds every public static function pointer field of the shared contract when it is shared, and **sharing fails**
(so no scripts load, and the engine logs why) if a field has no registration or a different signature. A mismatch can
therefore never become a null call. Rules to remember:

- Strings cross as UTF-8 bytes and a length; the wrapper does the encoding. Structs cross by pointer (Iris checks their
  layout with `Component::ValidateStruct`, from the component tickets onward). `bool` is one byte and `char` two,
  which is why the contract has `[assembly: DisableRuntimeMarshalling]`.
- A native function must not assume a thread: every script call happens on the main thread today, but the function
  should only touch what the App owns.
- Test it with a method on the fixture script assembly (`Tests/Fixture/Fixture.cs`) that calls the new wrapper, and a
  case in `Tests/ScriptingTests.cpp` that calls that method with `Call<...>` and checks what the engine saw.

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
