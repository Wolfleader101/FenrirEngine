# DemoApp

A windowed sample app: a renderer, an editor and two backpacks. It is also the smoke test of C# scripting in a real app (the proof
of the scripting package is its headless tests, in `packages/FenrirScripting/Tests`).

## With scripting (C#)

Needs the .NET SDK (the one Iris finds; .NET 10). Scripting is on in the clang presets (through `windows-clang-base`) and in
`msvc-x64-debug`: they set `FENRIR_ENABLE_SCRIPTING=ON`. Any other configuration defaults it to OFF.

```bash
cmake --preset clang-x64-debug
cmake --build out/build/clang-x64-debug --target DemoApp
```

Building `DemoApp` also builds the script project `scripts/DemoScripts.csproj` into `<exe dir>/scripts/`, which is where
`assets/demoApp.feproj` (`"scriptAssembly": "scripts/DemoScripts.dll"`) points. Run the demo from its own folder, as an IDE's run
button does:

```bash
cd out/build/clang-x64-debug/examples/DemoApp
./DemoApp.exe
```

The log shows `Scripting: loaded 'DemoScripts.dll'.` and the scripts start working.

### What the demo shows

Each piece is a small, commented file in `scripts/` (the C# half) and, where C++ is involved, `src/ScriptingDemo.hpp` (the C++ half).

| You see | What it shows | Where |
|---|---|---|
| Both backpacks turn about the Y axis. `R` reverses them. | A **component defined in C#** (`Spin`, nothing registered anywhere); a C# **system** that **queries** an engine component (`Transform`) together with it and writes the engine's memory directly. | `Spin.cs` |
| Backpack 2 bobs up and down, and its name in the editor's hierarchy keeps changing (`Backpack 2 (bob phase 3.75)`). | **A component shared between C++ and C#**: `Bob` is declared in C++, mirrored in C# with `[NativeComponent]`, and checked field by field at load. C++ sets how it looks, C# animates it and writes `Phase`, and a C++ system reads `Phase` back into the entity's `Name`. One copy of the data, nothing converted. | `Bobbing.cs`, `ScriptingDemo.hpp` |
| `R: reversed the spin.` in the log when you press R. | **An event from C++ to C#**: the window's existing C++ `KeyboardKeyEvent`, read from C# unchanged. | `Controls.cs` |
| Press `Space`: five `Spark N` entities appear in the hierarchy and vanish a couple of seconds later. | **Creating and destroying entities from C#**, giving them components and names, and **changes landing between systems** (the `Dresser` sees the sparks the `Spawner` made a moment before, the `Reaper`'s destroys never disturb its own loop). | `Sparks.cs` |
| `C++ saw a burst of 5 sparks sent from C#.` | **An event from C# to C++**: `Burst` is one queue read by both languages. | `Controls.cs`, `Sparks.cs`, `ScriptingDemo.hpp` |
| `Spark 3 burnt out.` | **An event only C# defines** (`Expired`): no C++ type and no registration; the build finds it and the engine makes its queue. | `Sparks.cs` |
| Edit a script while the demo runs (below). | **Hot reload**, with the data kept: the spin angles carry on. | all of them |

Each system reads events through readers of its own (`EventReader<T>` fields in C#, captured in the lambda in C++), so it handles each
event once, whichever language sent it.

### Running it without a keyboard

Set `FENRIR_DEMO_AUTOPLAY=1` and the demo presses `R` after a couple of seconds and `Space` after a couple more, by sending the same
`KeyboardKeyEvent` the window would. It is how the showcase was checked end to end:

```bash
cd out/build/clang-x64-debug/examples/DemoApp
FENRIR_DEMO_AUTOPLAY=1 ./DemoApp.exe
```

### Change the script while the demo runs

Leave the demo running, edit `scripts/Spin.cs` (change a speed, the log message), and rebuild only the scripts:

```bash
cmake --build out/build/clang-x64-debug --target DemoScripts
```

The demo notices the new build, reloads it between frames and logs `Scripting: reloaded 'DemoScripts.dll'`. The new code runs without
a restart, and each backpack's `Angle` (kept in the `Spin` component) carries on from where it was. See "Hot reload" in
`packages/FenrirScripting/README.md`.

## Without scripting

Configure with `-DFENRIR_ENABLE_SCRIPTING=OFF`. The demo builds and runs as it always did: no script project is built, no
`scripts` folder is made, and `dotnet` is never looked for or started.

## Editing the C# in VS Code

The script projects open without anything built: `FenrirScripting.sln` (at the repository root) holds the contract, the source
generator, `DemoScripts` and the test fixture. Point the C# extension at it with the setting
`"dotnet.defaultSolution": "FenrirScripting.sln"` (`.vscode/` is not committed, so this is yours to set), which also keeps the extension
from loading the unrelated legacy `.csproj` files under `libs/`. Completion, go-to-definition and the generated `Ref<T>` accessors (`transform.Position.Y`) all work, because a script
project in this repository imports `packages/FenrirScripting/Managed/Fenrir.Scripting.InRepo.props`, which references the contract and
generator sources when CMake has not supplied a built SDK folder. A script project outside the repository imports the SDK folder's
`Fenrir.Scripting.props` instead (see `packages/FenrirScripting/README.md`).
