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
| `FENRIR_BUILD_TESTS` | `OFF` | Builds the package tests with Catch2 (the `libs/catch2` submodule, or an installed Catch2 v3): FenrirECS always, FenrirScripting too when scripting is on. |

The Iris checks are built from Iris itself with `IRIS_BUILD_TESTS`, see `packages/Iris/README.md`. The first configure of a
fresh build directory can fail in the third-party assimp project; running the same configure again succeeds.
