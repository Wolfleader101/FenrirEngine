# 11: DemoApp sample script assembly

**What to build:** A small script project under `examples/DemoApp` (for example a spinning-entity system and a C# component) wired through `.feproj` and `Scripting::Install`, as a smoke test of the whole path in a real windowed app. It is not the test suite. The headless target from 01 remains the proof.

**Blocked by:** 07, 08

**Status:** resolved

- [x] With scripting ON, DemoApp visibly runs the sample system on scene entities.
- [x] With scripting OFF, DemoApp builds and runs unchanged, with no `dotnet`.
- [x] The README says how to build the script project and run the demo.

## Comments

### Resolution

- `examples/DemoApp/scripts/Spin.cs` replaces the placeholder `Hello.cs`: a C#-defined `Spin` component, a `SpinSetup` system that gives the two backpacks a `Spin` on the first frame (the entities are made by the app's Init systems, after scripts load, so `OnCreate` is too early), and a `Spinner` system that turns every entity with a `Transform` and a `Spin` about Y by writing the engine's `Transform`. The project file imports the scripting SDK.
- `DemoScripts` is now its own always-run CMake target that builds straight into `<exe dir>/scripts`. So `cmake --build ... --target DemoScripts` while the demo runs is the "rebuild the scripts" step, and the demo's hot reload picks it up.
- **Verified in the real windowed app (clang preset, Debug):** it logs `Scripting: loaded 'DemoScripts.dll'.` then `Spinning 2 entities from C#.`; with the app left running, editing the log message in `Spin.cs` and building the `DemoScripts` target logged `reloading`, `reloaded ... (generation 2)` and the new message. With `FENRIR_ENABLE_SCRIPTING=OFF` the demo builds and runs with no `scripts` folder, no scripting log lines, and no `dotnet` in the CMake cache.
- **Not verified:** the backpacks turning was not looked at on screen (the log and the Transform write are the evidence). The Spinner sets the rotation outright each frame, so it overrides any other rotation the editor gives these two entities while it runs.
- `examples/DemoApp/README.md` says how to build, run, and edit-and-reload.

Spec: `.scratch/engine-scripting/spec.md` (Build).
