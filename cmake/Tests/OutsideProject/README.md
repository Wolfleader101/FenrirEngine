# Outside project

A small project outside the engine that adds it with `add_subdirectory`, as a game does: the demo apps off, scripting
on, one material shader of its own compiled with `fenrir_add_shaders`, an executable linking every package, scripting's
run-time files deployed beside it with `fenrir_deploy_scripting`, and `outside_app_only`, which links only `FenrirApp`
and sets no C++ standard. Its CMake sets nothing the engine should give it, so a workaround needed here is an engine bug.

It is run by hand, not by CTest or CI (it builds the whole engine a second time): before a game's first ticket, and
after a change to the engine's CMake.

## Running it

From the checkout's root, in PowerShell, with the build directory kept short: the engine's binary directory is
`<build>/f`, and MSVC's deepest object path (glslang's) is about 180 characters below it.

```powershell
scripts\msvc.ps1 cmake -S cmake/Tests/OutsideProject -B out/o -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
```

```powershell
scripts\msvc.ps1 cmake --build out/o
```

```powershell
out\o\outside_project.exe
```

## What to check

- Configuring and building succeed, `outside_app_only` among the targets.
- `out/o/shaders/OutsideFlat.fshader` exists.
- `outside_project.exe` prints `outside-project: ran 3 frames` and exits 0, with `nethost.dll`, the `Iris.Bridge` files,
  `Fenrir.Scripting.dll`, `Fenrir.Math.dll` and `Fenrir.Physics.dll` beside it.
- The engine's outputs (`describe-api`, `generate-type-registry`, the Iris bridge, the scripting SDK, the contracts) are
  under `out/o/f`, none of them directly in `out/o`.
