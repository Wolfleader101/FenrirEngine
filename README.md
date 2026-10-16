# FenrirEngine

Modular Engine designed from the ground up to allow users to use only the modules they want to use.

## Currently a Work In Progress

## Building and testing

Third-party libraries are git submodules: after cloning, run `git submodule update --init --recursive`.

```sh
cmake --preset clang-x64-debug            # Windows; the presets turn scripting and the tests on
cmake --build out/build/clang-x64-debug
ctest --test-dir out/build/clang-x64-debug
```

| Option | Default | Effect |
| --- | --- | --- |
| `FENRIR_ENABLE_SCRIPTING` | `OFF` | Adds Iris and the C# scripting package (needs the .NET SDK). With it off, nothing looks for .NET. |
| `FENRIR_ENABLE_PHYSICS` | `ON` | Adds FenrirPhysics and its physics backend, JoltPhysics (the `packages/FenrirPhysics/libs/JoltPhysics` submodule, built as a static library), and the DemoApp's physics scene. Jolt supports the Debug and Release configurations; `RelWithDebInfo` and `MinSizeRel` build it without its debug-only checks. Jolt needs a CPU with AVX2. |
| `FENRIR_BUILD_TESTS` | `OFF` | Builds the package tests with Catch2 (the `libs/catch2` submodule, or an installed Catch2 v3): FenrirECS and FenrirApp always, FenrirPhysics when physics is on, and FenrirScripting when scripting is on. |

The Iris checks are built from Iris itself with `IRIS_BUILD_TESTS`, see `packages/Iris/README.md`. The first configure of a
fresh build directory can fail in the third-party assimp project; running the same configure again succeeds.
