# Demo scaffold

What every demo app is built on (`GLOSSARY.md`: demo app, demo scaffold): the `FenrirDemoScaffold` static library and
the `fenrir_add_demo_app` CMake function. Demo code, not engine code. Built with `FENRIR_BUILD_EXAMPLES` (on by
default) when `FENRIR_ENABLE_RENDERER` is on.

## A demo app

```cmake
fenrir_add_demo_app(RendererDemo SOURCES src/main.cpp src/MeshesAndMaterials.cpp ASSETS assets)
```

`fenrir_add_demo_app(<name> SOURCES … SCRIPTS … ASSETS …)` makes the executable and links the scaffold. Its asset
root, the `assets` folder beside the executable (FenrirAssets' default root), gets the shared `examples/assets/` and
then each `ASSETS` directory copied in after every build, so an app's file replaces a shared one of the same name. With
`FENRIR_BUILD_TESTS` on, it registers the app's smoke runs in CTest (below). `SCRIPTS` is accepted and not used yet:
building and copying the app's C# script project, and the `Fenrir.Demo` assembly, come with scripting in the demo apps.

```cpp
Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
FenrirDemo::DemoConfig config;
config.appName = "RendererDemo";
config.scenes = {RendererDemo::MeshesAndMaterials()};
FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
return demo.Run();
```

`DemoScaffold::Install(App&, DemoConfig)` installs FenrirWindow (for the OpenGL backend), FenrirInput, FenrirAssets,
FenrirDebugDraw and FenrirRenderer, and adds the scaffold's systems. Scripting joins with the scripting demo apps.
`DemoConfig::msaa` sets the renderer's MSAA samples (`RendererConfig::msaa`; 1, off, by default).
`Run()` runs the app and returns the process's exit code. Destroy the `DemoScaffold` before the `App`.

A `DemoScene` is one entry of the app's ordered scene list: a `name`, a one-line `keyHelp`, the `keys` a smoke run
presses, where the free-fly camera starts, a `spawn` function and an optional per-frame `update` (in `Update`, on the
main thread). Each time a scene is entered it is spawned into a FenrirScene of its own, named after it; leaving it
destroys that FenrirScene, and the assets the scene loaded for itself (owned by the scene) go with it.

## Keys

| Key | What it does |
| --- | --- |
| PageDown / PageUp | The next / previous scene, wrapping at both ends. |
| F1 | Shows or hides the help panel: the app, the scene and the scene's keys. Showing it also logs it. The panel is debug screen text in the window's bottom-left corner, clear of the on-screen messages, which stack from its top-left. |
| F2 | The app's (`DemoConfig::appKeyHelp` says what it does). |
| F3 | Turns the free-fly camera on or off. A scene that spawns an enabled `Camera` of its own starts with the free-fly camera off; F3 then switches between the two. |
| Right mouse button | Held, the mouse looks around, and WASD moves the free-fly camera (by key position), Shift makes it faster and the wheel changes its speed. Without it, WASD and the wheel are the scene's. |
| Left click | Sends `DemoSceneClickEvent`, the ray (`Renderer::ScreenPointToRay`) from the camera drawn at the clicked point (`Renderer::CameraAt`) through it. A click where no camera draws sends nothing. |

Every other key is the scene's.

## Smoke runs

Environment variables, read when the scaffold is installed:

- `FENRIR_DEMO_SCENE=<name>` starts in that scene (or runs only it in a smoke run). A name the app has no scene for is
  logged as an error.
- `FENRIR_DEMO_KEYS=1` presses the starting scene's keys, two seconds after it is entered and then one a second, and
  stops the app three seconds after the last. Each press logs `FENRIR_DEMO_KEYS: pressing key …`.
- `FENRIR_DEMO_SMOKE=<frames>` runs every scene (or the one `FENRIR_DEMO_SCENE` names) that many frames of 1/60 s, with
  the scene's keys pressed in turn across them, then stops. It exits with 1 if anything logged an error (GL debug
  errors included: the renderer's debug validation is on for smoke runs in every build), else 0. Its window is hidden,
  and vsync is off.
- `FENRIR_DEMO_BACKEND=null|opengl` picks the renderer's backend; `opengl` by default. `null` needs no window or GPU.

When the renderer cannot start (no OpenGL 4.6 core context), a smoke run exits with 4, which CTest reports as skipped.

```sh
FENRIR_DEMO_SMOKE=60 FENRIR_DEMO_BACKEND=null ./RendererDemo.exe
FENRIR_DEMO_SCENE="Meshes and materials" FENRIR_DEMO_KEYS=1 ./RendererDemo.exe
```

CTest runs each app twice: `<App>-Smoke-null` everywhere, and `<App>-Smoke-opengl`, labelled `gl`, which reports
skipped without a display or an OpenGL 4.6 context (CI's Windows jobs leave it out, as they do every `gl` test). Both
run `FENRIR_DEMO_SMOKE_FRAMES` frames per scene (a cache variable, 60 by default).

## Assets

Shared demo assets live in `examples/assets/`, and an app's own in `examples/<App>/assets/`.

- `cube.obj`: a cube of side 1 centred on the origin, with normals and UVs (`cube.obj#Mesh0`).
- `sphere.obj`: a UV sphere of radius 0.5, 32 segments by 16 rings, with normals and UVs (`sphere.obj#Mesh0`).
- `backpack/backpack.obj`: Berk Gedik's "Survival Guitar Backpack (Low Poly)", CC BY 4.0, with its diffuse, normal and
  specular textures; `backpack/attribution.txt` holds the attribution and the changes made to it.
- `skybox/skybox.cubemap`: a cubemap of six 512-pixel PNG faces, a procedural sky with hills and a sun, made for the
  engine by `skybox/make_skybox.py` (MIT, as the repository; `skybox/attribution.txt`).

The art-deco texture set comes with the scene that uses it.

## Tests

`FenrirDemoScaffoldTests` (CTest `FenrirDemoScaffold`) runs a demo app on the null backend with no window, its input
fed through FenrirInput's feed and the App driven one frame at a time, or run as a smoke run: the scene list, the free-
fly camera, the scene click, the help and the smoke runs.
