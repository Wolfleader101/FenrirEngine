# FenrirEngine

A modular C++20 game engine: every system is its own package, and you build only the ones you use.

> **Status: work in progress.** The engine is built in the open, package by package. APIs change without notice and there are no releases yet.

## What is in it

| Package | What it gives you |
| --- | --- |
| `FenrirECS`, `FenrirApp`, `FenrirScene`, `FenrirScheduler`, `FenrirTime` | The entity-component-system core, the app that hosts it, scenes, scheduling and time |
| `FenrirRenderer` | An ECS-facing renderer: `MeshRenderer`, `Camera` and lights, drawn through an OpenGL 4.6 backend (or a null backend for tests) with build-time compiled GLSL shaders |
| `FenrirWindow`, `FenrirInput` | An SDL3 window and graphics surface; keyboard, mouse and gamepad input as polled resources and events |
| `FenrirAssets`, `FenrirDraw2D` | Plain-data assets behind generational handles (PNG, JPEG, HDR, glTF/GLB, FBX, OBJ); 2D draw data for UIs |
| `FenrirPhysics` | Rigid bodies, joints, character bodies and vehicles over JoltPhysics, hidden behind the engine's own components |
| `FenrirDebugDraw` | Debug shapes and text, recorded per scene and drawn by the renderer |
| `FenrirScripting`, `Iris` | C# scripting: Iris hosts .NET (CoreCLR) with collectible, reloadable components; the engine exposes its packages to C# as `Fenrir.*` contracts |
| `FenrirMath`, `FenrirCore`, `FenrirLogger` | Math, core utilities and logging |

Design choices that shape the engine:

- **Modular by construction.** Each package is a CMake feature flag, so a headless physics sandbox does not pull in a renderer, and nothing looks for .NET unless scripting is on.
- **ECS first.** Game code draws a mesh by putting a `MeshRenderer` on an entity. C# scripts work on the same components.
- **Plain data between packages.** Shared types are described once in C++ headers, then `tools/describe-api` and the Iris binding generator produce the C# mirrors and registrations, and CI fails when they are stale.
- **Decisions are written down.** Over forty [architecture decision records](docs/adr) explain why things are the way they are, and [GLOSSARY.md](GLOSSARY.md) fixes the vocabulary.

## Quick start

You need CMake, a C++20 compiler (Clang or MSVC on Windows, GCC or Clang on Linux), and, only for scripting, the .NET 10 SDK.

```sh
git clone --recurse-submodules https://github.com/Wolfleader101/FenrirEngine.git
cd FenrirEngine
cmake --preset clang-x64-debug
cmake --build out/build/clang-x64-debug
ctest --test-dir out/build/clang-x64-debug
```

Already cloned without submodules? Run `git submodule update --init --recursive`. The presets are in [CMakePresets.json](CMakePresets.json); the options that switch packages on and off are in the table below.

## Demo apps

The [examples](examples) are the quickest way to see what the engine does, and the way scene changes are checked by hand:

| App | Shows | Needs |
| --- | --- | --- |
| [RendererDemo](examples/RendererDemo/README.md) | One scene per renderer feature | renderer |
| [PhysicsDemo](examples/PhysicsDemo/README.md) | Bodies, joints, characters, vehicles; C# scenes with scripting | renderer, physics |
| [ScriptingDemo](examples/ScriptingDemo/README.md) | C# scripts driving a scene | renderer, scripting |

All three sit on the [demo scaffold](examples/DemoScaffold/README.md), which documents the shared keys and the smoke-run switches.

## Documentation

- Each package has its own README next to its code: [FenrirRenderer](packages/FenrirRenderer/README.md), [FenrirInput](packages/FenrirInput/README.md), [FenrirWindow](packages/FenrirWindow/README.md), [FenrirScripting](packages/FenrirScripting/README.md), [FenrirDebugDraw](packages/FenrirDebugDraw/README.md), [FenrirDraw2D](packages/FenrirDraw2D/README.md) and [Iris](packages/Iris/README.md).
- [docs/adr](docs/adr) holds the architecture decisions; [GLOSSARY.md](GLOSSARY.md) the terms.
- [CODING_STANDARDS.md](CODING_STANDARDS.md) is what review checks against.

## Contributing

Run `git config core.hooksPath .githooks` once per clone to turn on the pre-commit hook (clang-format, and ticket headers in `.scratch`). Work is planned as markdown specs and tickets under `.scratch/<feature>/`. Before opening a PR, follow the build-and-test routine below.

## License

[MIT](LICENSE). Bundled third-party libraries keep their own licenses; the debug font is JetBrains Mono under the SIL Open Font License.

## Building and testing

Third-party libraries are git submodules: after cloning, run `git submodule update --init --recursive`.

```sh
cmake --preset clang-x64-debug            # Windows; the presets turn scripting and the tests on
cmake --build out/build/clang-x64-debug
ctest --test-dir out/build/clang-x64-debug
```

CI (`.github/workflows/build-and-test.yml`) runs these steps with the demo apps on pull requests only, for `msvc-x64-release`; changes to docs and tickets alone build nothing. The `linux-debug` job is commented out in the workflow to spare the Actions budget; when on, it also runs the GL tests (CTest label `gl`: the renderer's GL tests and the demo apps' OpenGL smoke runs) on Mesa's llvmpipe, through SDL's offscreen video driver over EGL; the Windows jobs run the null tests only, the demo apps' null smoke runs among them.

Before opening a PR, build and test `clang-x64-debug` (its CTest run includes each demo app's smoke runs, see
`examples/DemoScaffold/README.md`, and PhysicsDemo's per-scene log checks), and run each scene the change touches in
its demo app, RendererDemo, ScriptingDemo or PhysicsDemo, through its keys (`FENRIR_DEMO_SCENE=<scene>` with
`FENRIR_DEMO_KEYS=1`), checking it as that app's README's hand checks say. CI checks the other presets on the PR;
build one locally only to chase a failure CI reports there.

| Option | Default | Effect |
| --- | --- | --- |
| `FENRIR_ENABLE_SCRIPTING` | `OFF` | Adds Iris and the C# scripting package (needs the .NET SDK), FenrirInputScripting with the `Fenrir.Input` C# contract (input from C#), and FenrirDebugDrawScripting with the `Fenrir.DebugDraw` contract (debug shapes and text from C#). With `FENRIR_ENABLE_RENDERER` on as well, it also builds FenrirWindowScripting and the `Fenrir.Window` contract (the window from C#). With `FENRIR_ENABLE_ASSETS` on as well, it builds FenrirAssetsScripting and the `Fenrir.Assets` contract (loading assets and editing materials from C#), and with the renderer on too, FenrirRendererScripting and the `Fenrir.Renderer` contract (drawing, picking and spawning models from C#) and the demo apps' C# scripts. With it off, nothing looks for .NET and no extension target exists. |
| `FENRIR_ENABLE_PHYSICS` | `ON` | Adds FenrirPhysics and its physics backend, JoltPhysics (the `packages/FenrirPhysics/libs/JoltPhysics` submodule, built as a static library), and, with the examples, PhysicsDemo. With `FENRIR_ENABLE_SCRIPTING` on as well, it also builds FenrirPhysicsScripting and the `Fenrir.Physics` C# contract (physics from C#), and PhysicsDemo's C# scenes. Jolt supports the Debug and Release configurations; `RelWithDebInfo` and `MinSizeRel` build it without its debug-only checks. Jolt needs a CPU with AVX2. |
| `FENRIR_ENABLE_ASSETS` | `ON` | Adds FenrirAssets, the asset store, and FenrirDraw2D, 2D draw data as plain data for UIs (`packages/FenrirDraw2D`, header-only), with its importers as static libraries from submodules under `packages/FenrirAssets/libs/`: stb (a pinned commit; its `stb_image`) for PNG, JPEG and HDR images, cgltf (v1.15) for glTF and GLB models, and ufbx (v0.23.1) for FBX and OBJ models. The renderer needs it: `FENRIR_ENABLE_RENDERER` on with it off fails at configure. |
| `FENRIR_ENABLE_RENDERER` | `ON` | Adds FenrirWindow and SDL3 (the `packages/FenrirWindow/libs/SDL` submodule, built as a static library), and FenrirRenderer's build-time `FenrirShaderCompiler` with glslang and SPIRV-Reflect (the `packages/FenrirRenderer/Tools/ShaderCompiler/libs` submodules, static, linked by the tool only) and `fenrir_add_shaders` (`cmake/FenrirShaders.cmake`), which builds GLSL programs into shader containers, and the renderer core with its private graphics device, its null backend and its OpenGL 4.6 backend (`FenrirRendererCore`, `FenrirRendererDevice`, `FenrirRendererOpenGL`, headers not installed), the last with the glad 2 GL loader checked in under `packages/FenrirRenderer/libs/glad` (`FenrirGlad`, static, linked by the OpenGL backend only; its README holds the command that generated it, and the build runs no Python), and the debug font (`packages/FenrirRenderer/Resources/Fonts`, JetBrains Mono under the SIL Open Font License, embedded at build time) rasterised with stb_truetype from FenrirAssets's stb submodule (`FenrirStbTruetype`, static, linked by the renderer core only), and over them FenrirRenderer itself, the ECS facade (`Renderer::Install`, `MeshRenderer`, `Camera`, the lights; see `packages/FenrirRenderer/README.md`), the only renderer target with public headers. With it off, no window target exists. On Linux, SDL3's video needs the X11 and Wayland development packages (the CI workflow lists them). |
| `FENRIR_BUILD_TESTS` | `OFF` | Builds the package tests with Catch2 (the `libs/catch2` submodule, or an installed Catch2 v3): FenrirMath, FenrirECS, FenrirApp, FenrirInput and FenrirDebugDraw always, FenrirAssets and FenrirDraw2D when assets are on, FenrirWindow, FenrirShaderCompiler and FenrirRenderer when the renderer is on (the window tests report skipped without a display; the renderer's GL tests, CTest label `gl`, report skipped without a display or an OpenGL 4.6 core context, so run them on your own GPU before a PR that touches the OpenGL backend), FenrirPhysics when physics is on, FenrirScripting when scripting is on, and physics from C# (FenrirPhysicsScripting) when both are. |
| `FENRIR_BUILD_EXAMPLES` | `ON` | Builds the demo apps on the demo scaffold (`examples/DemoScaffold/README.md`): `FenrirDemoScaffold` (with Dear ImGui for its ImGui overlay, the `examples/DemoScaffold/libs/imgui` submodule, built as a static library and linked by the scaffold only), `fenrir_add_demo_app` and RendererDemo (`examples/RendererDemo/README.md`) and, with `FENRIR_ENABLE_SCRIPTING`, ScriptingDemo (`examples/ScriptingDemo/README.md`), and, with `FENRIR_ENABLE_PHYSICS`, PhysicsDemo (`examples/PhysicsDemo/README.md`), with their tests and CTest smoke runs when `FENRIR_BUILD_TESTS` is on. They draw, so they exist only with `FENRIR_ENABLE_RENDERER`. |

Packages that share types with bindings commit an API description (`packages/<Package>/api/<Package>.api.json`, ADR-0042),
generated from their headers by `tools/describe-api`. The ordinary build never regenerates them:
`cmake --build out/build/clang-x64-debug --target fenrir-describe-api` does (it needs the .NET SDK), and CI fails on a pull request
whose descriptions are stale. See `tools/describe-api/README.md`.

A C# binding is generated from those descriptions and committed too: FenrirPhysics's components, events and shared
structs are written once, in its headers, and `packages/Iris/Tools/Iris.Bindings` generates their C# mirrors
(`packages/FenrirPhysics/Managed/Fenrir.Physics/Generated`), their Iris mappings and the extension's registration lists
(`packages/FenrirPhysics/Scripting/src/Generated`) and a defaults test (`packages/FenrirPhysics/Scripting/Tests/Generated`)
from `packages/FenrirPhysics/Scripting/api/FenrirPhysics.binding.toml`. Its native functions are written once too, as their
C++ definitions in `packages/FenrirPhysics/Scripting/src/PhysicsScripting.cpp`: `fenrir-describe-api` describes them into
`FenrirPhysics.natives.json` beside the binding's configuration, and the binding generates their C# declarations
(`PhysicsNative`), their registration and a test of their outputs. FenrirScripting's built-in extension is bound the same
way, from FenrirECS's description (the engine's Transform, Name, Relationship and Entity) and its natives in
`packages/FenrirScripting/src/Bindings.cpp`, by `packages/FenrirScripting/api/FenrirScripting.binding.toml`, and so is
every other engine extension: FenrirInput's, FenrirWindow's, FenrirAssets's, FenrirRenderer's and FenrirDebugDraw's, each by
its `packages/<Package>/Scripting/api/<Package>.binding.toml`. The Fenrir.Math contract's colour, ray and box are generated
by `packages/FenrirScripting/api/FenrirMath.binding.toml`, and every other binding names them through `uses` rather than
mirroring them again (ADR-0043).
After changing a shared type or a native, run
`fenrir-describe-api`, then `cmake --build out/build/clang-x64-debug --target fenrir-generate-bindings`, and commit both;
CI fails on a stale binding as on a stale description. What each contract still holds by hand is its facades (the static
classes scripts call, such as `Keyboard` or `Renderer`) and the behaviour of its mirrors, as `partial` parts beside the
generated ones.

The Iris checks are built from Iris itself with `IRIS_BUILD_TESTS`, see `packages/Iris/README.md`.
