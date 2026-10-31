# ScriptingDemo

The scripting showcase, on the demo scaffold (`examples/DemoScaffold/README.md`, for the keys every demo app shares and
the smoke-run switches): the legacy DemoApp's scripting demo, ported unchanged. It has one scene, "Showcase", and exists
only with scripting built (`FENRIR_ENABLE_SCRIPTING`, on in the clang presets and `msvc-x64-debug`; needs the .NET 10
SDK).

```sh
cmake --build out/build/clang-x64-debug --target ScriptingDemo
cd out/build/clang-x64-debug/examples/ScriptingDemo
./ScriptingDemo.exe
FENRIR_DEMO_SMOKE=900 FENRIR_DEMO_BACKEND=null ./ScriptingDemo.exe
```

Building `ScriptingDemo` also builds the scripts project `scripts/ScriptingDemo.Scripts.csproj` into `scripts/` beside the
executable. The log shows `Scripting: loaded 'ScriptingDemo.Scripts.dll'.` and the scripts start working.

## What it shows

Each piece is a small, commented file in `scripts/` (the C# half) and, where C++ is involved, `src/Showcase.cpp` (the C++
half). The legacy editor's hierarchy is gone, so the names it showed are drawn as world text instead: each box's
above it, and each spark's where it is.

| You see | What it shows | Where |
|---|---|---|
| Both boxes turn about the Y axis. `R` reverses them. | A **component defined in C#** (`Spin`, nothing registered anywhere); a C# **system** that **queries** an engine component (`Transform`) together with it and writes the engine's memory directly. | `Spin.cs` |
| Box 2 bobs up and down, and its name above it keeps changing (`Box 2 (bob phase 3.75)`). | **A component shared between C++ and C#**: `Bob` is declared in C++, mirrored in C# with `[NativeComponent]`, and checked field by field at load. C++ sets how it looks, C# animates it and writes `Phase`, and a C++ system reads `Phase` back into the entity's `Name`. One copy of the data, nothing converted. | `Bobbing.cs`, `Showcase.cpp` |
| `R: reversed the spin.` in the log when you press R. | **An event from C++ to C#**: FenrirInput's C++ `KeyEvent`, read from C# unchanged (`Fenrir.Input.KeyEvent`). | `Controls.cs` |
| Press `Space`: five `Spark N` names appear in a row above the boxes and vanish a couple of seconds later. | **Creating and destroying entities from C#**, giving them components and names, and **changes landing between systems** (the `Dresser` sees the sparks the `Spawner` made a moment before, the `Reaper`'s destroys never disturb its own loop). | `Sparks.cs` |
| `C++ saw a burst of 5 sparks sent from C#.` | **An event from C# to C++**: `Burst` is one queue read by both languages. | `Controls.cs`, `Sparks.cs`, `Showcase.cpp` |
| `Spark 3 burnt out.` | **An event only C# defines** (`Expired`): no C++ type and no registration; the build finds it and the engine makes its queue. | `Sparks.cs` |
| Edit a script while the demo runs (below). | **Hot reload**, with the data kept: the spin angles carry on. | all of them |

Each system reads events through readers of its own (`EventReader<T>` fields in C#, captured in the lambda in C++), so it
handles each event once, whichever language sent it. `Bob` and `Burst` are the app's own scripting extension
(`ScriptingDemo::Extension()`, given to the scaffold as `DemoConfig::scriptingExtensions`); the boxes are the shared
`BoxTextured/BoxTextured.gltf` (Khronos's textured box), spawned with `SpawnModel` and drawn by `MeshRenderer`.

| Key | Does |
|---|---|
| `R` | Reverses the boxes' spin. |
| `Space` | A burst of five sparks. |

### Change the script while the demo runs

Leave the demo running, edit `scripts/Spin.cs` (change a speed, the log message), and rebuild only the scripts:

```sh
cmake --build out/build/clang-x64-debug --target ScriptingDemoDeployScripts
```

The demo notices the new build, reloads it between frames and logs `Scripting: reloaded 'ScriptingDemo.Scripts.dll'`. The
new code runs without a restart, and each box's `Angle` (kept in the `Spin` component) carries on from where it was.
See "Hot reload" in `packages/FenrirScripting/README.md`. Smoke runs turn hot reload off.

## Smoke runs

CTest runs `ScriptingDemo-Smoke-null` and `ScriptingDemo-Smoke-opengl`, as for every demo app, and
`ScriptingDemo-Showcase-null` and `ScriptingDemo-Showcase-opengl`: 900 frames of Showcase with R and Space pressed, which
pass only if the log has the lines above (the spin, `Bob.Phase` read by C++, the key event, the burst and a burnt-out
spark; `cmake/CheckShowcaseLog.cmake`).

## Hand checks

- The window shows Fenrir's icon (`examples/assets/icons/fenrir.png`, `DemoConfig::iconPath`) in its title bar and on
  its taskbar button, not the system's default.
- ScriptingDemo opens a window on Showcase: the two textured, lit boxes, one above the other, turning about the Y
  axis in opposite directions; the log says `Spinning 2 entities from C#. Press R to reverse them, Space for sparks.`
- The upper box bobs up and down, and the name above it, `Box 2 (bob phase …)`, counts up as it does; the log
  says `C++ read Bob.Phase = …, which C# wrote into the same component.` once.
- R reverses both spins, and the log says `R: reversed the spin.`; R again turns them back.
- Space puts five `Spark N` names in a row above the boxes, logs `C++ saw a burst of 5 sparks sent from C#.`, and
  over the next two to four seconds each name vanishes as `Spark N burnt out.` is logged. Several presses number the
  sparks on (`Spark 6`, `Spark 7`…).
- Hot reload: with the demo running, change `RadiansPerSecond` in `scripts/Spin.cs`'s `SpinSetup` to `3.0f` and the log
  message, and build `ScriptingDemoDeployScripts`. The log says `Scripting: reloaded 'ScriptingDemo.Scripts.dll'`, the
  boxes keep turning from where they were, without a jump, and PageDown (which enters Showcase again) spawns them
  turning at the new speed with the new message.
