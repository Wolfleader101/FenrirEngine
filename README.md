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

CI (`.github/workflows/build-and-test.yml`) runs these steps without the DemoApp: on pull requests for `clang-x64-debug`, `msvc-x64-release` and `linux-debug`, and on pushes to `dotnet-scripting` and `main` for all four clang and MSVC x64 presets and `linux-debug`.

Before opening a PR, build and test `clang-x64-debug`, and run the DemoApp smoke run of each scene the change touches
(`FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1`, see `examples/DemoApp/README.md`). `scripts/demo-smoke.sh` runs every physics scene's
smoke run at once, sped up and with no window shown, and reports which failed. CI checks the other presets on the PR;
build one locally only to chase a failure CI reports there.

| Option | Default | Effect |
| --- | --- | --- |
| `FENRIR_ENABLE_SCRIPTING` | `OFF` | Adds Iris and the C# scripting package (needs the .NET SDK). With it off, nothing looks for .NET. |
| `FENRIR_ENABLE_PHYSICS` | `ON` | Adds FenrirPhysics and its physics backend, JoltPhysics (the `packages/FenrirPhysics/libs/JoltPhysics` submodule, built as a static library), and the DemoApp's physics scene. With `FENRIR_ENABLE_SCRIPTING` on as well, it also builds FenrirPhysicsScripting and the `Fenrir.Physics` C# contract (physics from C#), and the DemoApp gives scripting physics's extension. Jolt supports the Debug and Release configurations; `RelWithDebInfo` and `MinSizeRel` build it without its debug-only checks. Jolt needs a CPU with AVX2. |
| `FENRIR_BUILD_TESTS` | `OFF` | Builds the package tests with Catch2 (the `libs/catch2` submodule, or an installed Catch2 v3): FenrirECS and FenrirApp always, FenrirPhysics when physics is on, FenrirScripting when scripting is on, and physics from C# (FenrirPhysicsScripting) when both are. |
| `FENRIR_BUILD_EXAMPLES` | `ON` | Builds the DemoApp. Its third-party libraries (`examples/DemoApp/libs`) are not submodules, so turn it off on a fresh clone (CI does). |

Packages that share types with bindings commit an API description (`packages/<Package>/api/<Package>.api.json`, ADR-0016),
generated from their headers by `tools/describe-api`. The ordinary build never regenerates them:
`cmake --build out/build/clang-x64-debug --target fenrir-describe-api` does (it needs the .NET SDK), and CI fails on a pull request
whose descriptions are stale. See `tools/describe-api/README.md`.

A C# binding is generated from those descriptions and committed too: FenrirPhysics's components, events and shared
structs are written once, in its headers, and `packages/Iris/Tools/Iris.Bindings` generates their C# mirrors
(`packages/FenrirPhysics/Managed/Fenrir.Physics/Generated`), their Iris mappings and the extension's registration lists
(`packages/FenrirPhysics/Scripting/src/Generated`) and a defaults test (`packages/FenrirPhysics/Scripting/Tests/Generated`)
from `packages/FenrirPhysics/Scripting/api/FenrirPhysics.binding.toml`. After changing a shared type, run
`fenrir-describe-api`, then `cmake --build out/build/clang-x64-debug --target fenrir-generate-bindings`, and commit both;
CI fails on a stale binding as on a stale description.

The Iris checks are built from Iris itself with `IRIS_BUILD_TESTS`, see `packages/Iris/README.md`. The first configure of a
fresh build directory can fail in the third-party assimp project; running the same configure again succeeds.
