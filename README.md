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

CI (`.github/workflows/build-and-test.yml`) runs these steps with the demo apps and without the legacy DemoApp: on pull requests for `clang-x64-debug`, `msvc-x64-release` and `linux-debug`, and on pushes to `dotnet-scripting` and `main` for all four clang and MSVC x64 presets and `linux-debug`. `linux-debug` also runs the GL tests (CTest label `gl`: the renderer's GL tests and the demo apps' OpenGL smoke runs) on Mesa's llvmpipe, through SDL's offscreen video driver over EGL; the Windows jobs run the null tests only, the demo apps' null smoke runs among them.

Before opening a PR, build and test `clang-x64-debug` (its CTest run includes each demo app's smoke runs, see
`examples/DemoScaffold/README.md`), and run the DemoApp smoke run of each scene the change touches
(`FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1`, see `examples/DemoApp/README.md`). `scripts/demo-smoke.sh` runs every physics scene's
smoke run at once, sped up and with no window shown, and reports which failed. CI checks the other presets on the PR;
build one locally only to chase a failure CI reports there.

| Option | Default | Effect |
| --- | --- | --- |
| `FENRIR_ENABLE_SCRIPTING` | `OFF` | Adds Iris and the C# scripting package (needs the .NET SDK), and FenrirInputScripting with the `Fenrir.Input` C# contract (input from C#). With `FENRIR_ENABLE_RENDERER` on as well, it also builds FenrirWindowScripting and the `Fenrir.Window` contract (the window from C#). With `FENRIR_ENABLE_ASSETS` on as well, it builds FenrirAssetsScripting and the `Fenrir.Assets` contract (loading assets and editing materials from C#), and with the renderer on too, FenrirRendererScripting and the `Fenrir.Renderer` contract (drawing, picking and spawning models from C#) and the demo apps' C# scripts. With it off, nothing looks for .NET and no extension target exists. |
| `FENRIR_ENABLE_PHYSICS` | `ON` | Adds FenrirPhysics and its physics backend, JoltPhysics (the `packages/FenrirPhysics/libs/JoltPhysics` submodule, built as a static library), and the DemoApp's physics scene. With `FENRIR_ENABLE_SCRIPTING` on as well, it also builds FenrirPhysicsScripting and the `Fenrir.Physics` C# contract (physics from C#), and the DemoApp gives scripting physics's extension. Jolt supports the Debug and Release configurations; `RelWithDebInfo` and `MinSizeRel` build it without its debug-only checks. Jolt needs a CPU with AVX2. |
| `FENRIR_ENABLE_ASSETS` | `ON` | Adds FenrirAssets, the asset store, and FenrirDraw2D, 2D draw data as plain data for UIs (`packages/FenrirDraw2D`, header-only), with its importers as static libraries from submodules under `packages/FenrirAssets/libs/`: stb (a pinned commit; its `stb_image`) for PNG, JPEG and HDR images, cgltf (v1.15) for glTF and GLB models, and ufbx (v0.23.1) for FBX and OBJ models. The renderer needs it: `FENRIR_ENABLE_RENDERER` on with it off fails at configure. |
| `FENRIR_ENABLE_RENDERER` | `ON` | Adds FenrirWindow and SDL3 (the `packages/FenrirWindow/libs/SDL` submodule, built as a static library), and FenrirRenderer's build-time `FenrirShaderCompiler` with glslang and SPIRV-Reflect (the `packages/FenrirRenderer/Tools/ShaderCompiler/libs` submodules, static, linked by the tool only) and `fenrir_add_shaders` (`cmake/FenrirShaders.cmake`), which builds GLSL programs into shader containers, and the renderer core with its private graphics device, its null backend and its OpenGL 4.6 backend (`FenrirRendererCore`, `FenrirRendererDevice`, `FenrirRendererOpenGL`, headers not installed), the last with the glad 2 GL loader checked in under `packages/FenrirRenderer/libs/glad` (`FenrirGlad`, static, linked by the OpenGL backend only; its README holds the command that generated it, and the build runs no Python), and the debug font (`packages/FenrirRenderer/Resources/Fonts`, JetBrains Mono under the SIL Open Font License, embedded at build time) rasterised with stb_truetype from FenrirAssets's stb submodule (`FenrirStbTruetype`, static, linked by the renderer core only), and over them FenrirRenderer itself, the ECS facade (`Renderer::Install`, `MeshRenderer`, `Camera`, the lights; see `packages/FenrirRenderer/README.md`), the only renderer target with public headers. With it off, no window target exists. On Linux, SDL3's video needs the X11 and Wayland development packages (the CI workflow lists them). |
| `FENRIR_BUILD_TESTS` | `OFF` | Builds the package tests with Catch2 (the `libs/catch2` submodule, or an installed Catch2 v3): FenrirMath, FenrirECS, FenrirApp, FenrirInput and FenrirDebugDraw always, FenrirAssets and FenrirDraw2D when assets are on, FenrirWindow, FenrirShaderCompiler and FenrirRenderer when the renderer is on (the window tests report skipped without a display; the renderer's GL tests, CTest label `gl`, report skipped without a display or an OpenGL 4.6 core context, so run them on your own GPU before a PR that touches the OpenGL backend), FenrirPhysics when physics is on, FenrirScripting when scripting is on, and physics from C# (FenrirPhysicsScripting) when both are. |
| `FENRIR_BUILD_EXAMPLES` | `ON` | Builds the demo apps on the demo scaffold (`examples/DemoScaffold/README.md`): `FenrirDemoScaffold` (with Dear ImGui for its ImGui overlay, the `examples/DemoScaffold/libs/imgui` submodule, built as a static library and linked by the scaffold only), `fenrir_add_demo_app` and RendererDemo (`examples/RendererDemo/README.md`), with their tests and CTest smoke runs when `FENRIR_BUILD_TESTS` is on. They draw, so they exist only with `FENRIR_ENABLE_RENDERER`. |
| `FENRIR_BUILD_LEGACY_DEMOAPP` | `OFF` | Builds the legacy DemoApp (`examples/DemoApp`), until the demo apps replace it. Its third-party libraries (`examples/DemoApp/libs`) are not submodules, so leave it off on a fresh clone (CI does). It skips its own assimp when the build already has an `assimp` target. |

The Iris checks are built from Iris itself with `IRIS_BUILD_TESTS`, see `packages/Iris/README.md`. With the legacy DemoApp on, the first configure of a
fresh build directory can fail in the third-party assimp project; running the same configure again succeeds.
