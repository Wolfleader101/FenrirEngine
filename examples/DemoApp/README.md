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

The log shows `Scripting: loaded 'DemoScripts.dll'.` and then `Spinning 2 entities from C#.`: `scripts/Spin.cs` defines a
`Spin` component in C# and two systems. `SpinSetup` gives the two backpacks a `Spin`; `Spinner` turns every entity that has a
`Transform` and a `Spin` about the Y axis by writing the engine's own `Transform`. The backpacks turn on screen.

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
