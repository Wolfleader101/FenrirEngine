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

## Writing a script project

A script project needs only the scripting SDK: one folder holding `Fenrir.Scripting.dll` (the contract),
`analyzers/Fenrir.Scripting.Generators.dll` (the source generator) and `Fenrir.Scripting.props`. CMake builds it into
`<build>/fenrir-scripting/contract/`. Import the props file and nothing else is needed:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
  </PropertyGroup>
  <Import Project="$(FenrirScriptingSdk)Fenrir.Scripting.props" />   <!-- dotnet build -p:FenrirScriptingSdk=<folder>/ -->
</Project>
```

The props file references the contract for compilation only (the host shares its own copy at run time), turns on
runs the generator (the field accessors are C# 14 extension properties, the default for net10.0). A script's output must not carry its own copy of `Fenrir.Scripting.dll`.

### Components and `Ref<T>`

A component is an `unmanaged` struct implementing `IComponent`. `Ref<T>` is a `ref struct` over a pointer into the
engine's storage, and for every component the generator adds one extension property per public (or internal)
instance field, returning `ref` to that field:

```csharp
public struct Vec3 { public float X, Y, Z; }
public struct Transform : IComponent { public Vec3 Position; public float Scale; }

void Nudge(Ref<Transform> t)
{
    t.Position.X += 1;     // writes the engine's memory: no `ref` at the call site
    t.Scale *= 2;
    Transform copy = t.Value;   // the whole component (a copy when assigned to a variable)
}
```

- **How it works.** The generated code is a C# 14 `extension(Ref<Transform> self)` block with
  `public ref Vec3 Position => ref self.Value.Position;`. A property that returns `ref` is a variable, so
  `t.Position.X += 1` assigns through it. The `t.Value.Position.X` spelling the spec kept as a fallback also works,
  but is not needed.
- **What gets a property.** Public and internal instance fields. Not exposed: static and `const` fields, `readonly`
  fields (there is no `ref` to give), and fixed buffers. A field named `Value` or `IsNull` would be shadowed by
  `Ref<T>`'s own members, so the generator warns (`FENRIR0001`) and it stays reachable as `t.Value.Value`. A generic
  component warns (`FENRIR0002`). A keyword-named field (`@class`) works. A nested or internal component gets an
  accessors class of the matching accessibility. A `partial` component works, but keep all of its fields in one
  declaration: the compiler warns (`CS0282`) that field order across declarations is undefined, and a component's
  layout is what native code relies on.
- **A `Ref<T>` is only good for the call that was handed it.** It points into storage the engine may move, so the
  compiler refuses every way of keeping one. Each is a compile-fail test: a field of a class (`CS8345`), a lambda
  capturing it (`CS9108`), `List<Ref<T>>` (`CS9244`) and an `async` method taking it (`CS4012`), against a control case
  that compiles.
- **A `Ref<T>` can be null** (`IsNull`); using `Value` on one throws `InvalidOperationException` naming the type.

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
(`Tests/Fixture`), built with the same .NET SDK Iris uses against the scripting SDK folder. The `RefCompile-*` tests
build the small projects in `Tests/CompileFail` and check the compiler's verdict. Run everything with CTest:

```sh
cmake -S . -B out/build/engine-scripting -DFENRIR_ENABLE_SCRIPTING=ON
cmake --build out/build/engine-scripting --config Debug --target FenrirScriptingTests
ctest --test-dir out/build/engine-scripting -C Debug --output-on-failure
```

`Tests/HeadlessApp.hpp` is the harness later tickets build on: `HeadlessApp` owns an `App` and a recording
logger, and `Run(n)` runs exactly `n` frames.

The first configure of a fresh build directory can fail in the third-party assimp project; running the same
configure command again succeeds. That happens with scripting OFF too.
