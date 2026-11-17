# Demo scaffold

What every demo app is built on (`GLOSSARY.md`: demo app, demo scaffold): the `FenrirDemoScaffold` static library and
the `fenrir_add_demo_app` CMake function. Demo code, not engine code. Built with `FENRIR_BUILD_EXAMPLES` (on by
default) when `FENRIR_ENABLE_RENDERER` and `FENRIR_ENABLE_UI` are on.

## A demo app

```cmake
fenrir_add_demo_app(RendererDemo SOURCES src/main.cpp src/MeshesAndMaterials.cpp ASSETS assets)
```

`fenrir_add_demo_app(<name> SOURCES … SCRIPTS … ASSETS …)` makes the executable and links the scaffold. Its asset
root, the `assets` folder beside the executable (FenrirAssets' default root), gets the shared `examples/assets/`, the
demo UI kit's files (`examples/DemoScaffold/assets/`) and then each `ASSETS` directory copied in after every build, so an app's file replaces a shared one of the same name. With
`FENRIR_BUILD_TESTS` on, it registers the app's smoke runs in CTest (below). `SCRIPTS <dir>` names the app's C# scripts
project, `<dir>/<App>.Scripts.csproj` (see "C# scenes").

```cpp
Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
FenrirDemo::DemoConfig config;
config.appName = "RendererDemo";
config.scenes = {RendererDemo::MeshesAndMaterials()};
FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
return demo.Run();
```

`DemoScaffold::Install(App&, DemoConfig)` installs FenrirWindow (for the OpenGL backend), FenrirInput, FenrirAssets,
FenrirDebugDraw, FenrirRenderer and FenrirUI, adds the demo UI kit (below), and adds the scaffold's systems. With
scripting built and `DemoConfig::scriptAssembly` set, it installs Scripting last (see "C# scenes"), with the text,
painter and UI scripting extensions among the packages' where they are built. An app installs no FenrirUI of its own
(a second `UI::Install` is refused): its roots go in the scaffold's.
`DemoConfig::installPackages`, when set, runs after FenrirUI and the kit and before Scripting, for an app's own packages
that need the scaffold's and must precede Scripting (AnimationDemo's FenrirSkeleton, whose pose stages a script's
systems join as it loads, and UIDemo's roots); the scaffold keeps what it returns and destroys it after Scripting ends
and before the kit and its own packages.
`DemoConfig::msaa` sets the renderer's MSAA samples (`RendererConfig::msaa`; 1, off, by default).
`DemoConfig::iconPath` names the window's icon, an RGBA8 PNG or JPEG under the asset root, which the scaffold loads
through FenrirAssets and gives to `Window::SetIcon`; it is the shared `icons/fenrir.png` by default, and empty keeps
SDL's default icon. A smoke run's hidden window gets it too.
`Run()` runs the app and returns the process's exit code. Destroy the `DemoScaffold` before the `App`.

A `DemoScene` is one entry of the app's ordered scene list: a `name`, its `keyRows` for the help (or a one-line
`keyHelp`, which the help shows as one row), the `keys` a smoke run presses, where the free-fly camera starts, a `spawn` function and an optional per-frame `update` (in `Update`, on the
main thread). Each time a scene is entered it is spawned into a FenrirScene of its own, named after it; leaving it
destroys that FenrirScene, and the assets the scene loaded for itself (owned by the scene) go with it.

## C# scenes

With `FENRIR_ENABLE_SCRIPTING` on, an app can run C# scenes:

- **The scripts project**, `<App>.Scripts.csproj` in the directory `SCRIPTS` names, references the contracts and the
  scaffold's shared `Fenrir.Demo` assembly (`Managed/Fenrir.Demo`) with `HintPath`s to the `Fenrir<Name>ContractDir`
  folders `fenrir_add_demo_app` passes (RendererDemo's `scripts/` is the example). `FenrirPhysicsContractDir` is passed
  too whenever physics's scripting extension is built, for an app that installs physics itself (PhysicsDemo). It is built to `scripts/` beside the
  executable, with the contracts and what scripting needs at run time, and the app's sources get
  `FENRIR_DEMO_SCRIPT_ASSEMBLY`, its path, for `DemoConfig::scriptAssembly`. Without scripting, `SCRIPTS` is ignored and
  `FENRIR_DEMO_SCRIPT_ASSEMBLY` is not defined.
- **Scripting** runs the scripts with every installed package's extension (Input, Window, Assets, Renderer,
  DebugDraw) and the scaffold's own, which shares `Fenrir.Demo`: the `CSharpScene` marker, a read-only scene resource, and
  `DemoSceneClickEvent`, the scene click as a native event, and then the app's own, `DemoConfig::scriptingExtensions`
  (ScriptingDemo's `Bob` and `Burst` are the example). A script assembly that fails to load is logged as an error,
  so it fails a smoke run. Hot reload is on, except in a smoke run.
- **A C# scene** is a `DemoScene` with a nonzero `csharpScene` number and no `spawn`. Each time it is entered its
  FenrirScene gets a `CSharpScene { Scene, Entry }`, `Entry` new for each entry. Its C# systems do nothing unless
  `CSharpScene.IsActive(number, out entry)`, and spawn the scene when `entry` is one they have not seen; the scene's
  assets and entities go with its FenrirScene, as a C++ scene's do.

## The demo UI kit

The demo UI kit (`GLOSSARY.md`, ADR-0071) is the scaffold's UI over FenrirUI, in every demo app:
`include/FenrirDemoScaffold/DemoUI.hpp` and `src/DemoUI.cpp`, in namespace `FenrirDemo`. Its API names no scaffold
type: the scaffold feeds it. The scaffold adds it as the `DemoUI` app resource once FenrirUI is installed.

- **The root:** one `UIRoot` on the main window's overlay, at layer `DemoUILayer` with input priority
  `DemoUIRouterPriority` (both 900): over every demo app's own roots (UIDemo's HUD is at 0 and its menu at 10), under
  the ImGui overlay's 1000 while that is there. Debug screen text and on-screen messages draw over it.
- **The demo panel**, on the right, 360 UI units wide at the window's `Height1080` scale and scrolling when taller than
  the window, under the app's name (`SetTitle`): `AppSection(title)` adds a section of the app's, which stays, and
  `SceneSection(title)` one of the scene's, after the app's. `ClearScene()` removes the scene's sections; the scaffold
  calls it when a scene is left, before its FenrirScene goes, so a row bound to the scene's state never outlives it.
  A section (`DemoSection`, a handle) has a heading that collapses and opens it, and rows in the order they were
  added:
  - `Text(line)`: a read-only line that `DemoTextRow::Set` changes.
  - `Readout(label)`: a readout (`GLOSSARY.md`), its label and its value in two columns (`.demo-readout-label` and
    `.demo-readout-value` in `demo.css`). `DemoReadout::Set(value)` shows a value, changing the element only when it
    differs, so a scene's `update` may set it every frame; it shows that frame.
  - `Readout(label, std::function<std::string()>)`: a readout pulled once a frame in the kit's build stage work, after
    the scenes' `Update`. A scene section's function is no longer called once the scene is left, so it may hold
    pointers into the scene's state.
  - `Separator(title)`: a rule, under a title when one is given, grouping the rows after it.

  Every row handle can hide its row (`SetShown`, taking no room) and grey it out (`SetDisabled`). A handle to a section
  or row that is gone does nothing.
- **The scene switcher** is the panel's first app section, "Scenes": a button per scene of the app, in order, the
  active one with the class `active`. A click enters that scene the next frame, as PageDown does, with the same
  `Scene i/n` log line and help, and in a smoke run too; a click on the active one does nothing. The scaffold feeds
  it (`DemoUI::SetScenes` and `SetActiveScene`, which name no scaffold type).
- **Frame stats:** `DemoConfig::frameStats = true` adds the app section "Frame" after the switcher: the frame time and
  fps, each the average of a half-second window, and the draws, instances and shadow draws of the renderer's last
  frame (`Renderer::Stats`). Both are added before `installPackages`, so they come before the app's own sections.
- **The help**, at the bottom left, clear of the on-screen messages that stack from the top left: a section that starts
  open, with the app and the scene (`<App>: <scene> (i/n)`), then key rows (`DemoKeyRow{keys, what}`): the scaffold's
  (the keys below), then the app's (`DemoConfig::appKeyRows`, or `appKeyHelp` as one F2 row), then the scene's
  (`DemoScene::keyRows`, or `keyHelp` as one row). The scaffold sets it each time a scene is entered (`SetHelp`). While
  the kit shows, the help is logged as `Help: ...` lines when it is set and when F1 shows it.
- **F1** hides or shows all of the kit (`SetVisible`, `IsVisible`), even while one of its controls has the focus,
  unless that is a text entry; a held F1 toggles once. While another root has the focus (UIDemo's open menu), that root
  has the keyboard and F1 with it. Hidden, the kit takes no input and gives up the focus.
- **Input:** only the panels' boxes take the pointer (`pointer-events: all` on `.demo-box`), so a press on a panel is
  consumed and sends no `DemoSceneClickEvent`, while the free-fly camera's right-drag and the scene click work everywhere
  else (ADR-0065). Tab and a click focus the kit's controls as FenrirUI does, and a focused one has the keyboard until
  a click on the scene. The root takes no gamepad input (`UIRootConfig::gamepadNavigation` off), so a pad always drives
  the scene. The window's text input follows the kit's `textInput` claim (`WindowConfig::textInputByDefault` is false).
- **Files and hot reload:** `assets/ui/demo.ui` (the containers, by id) and `assets/ui/demo.css` (the one theme: its
  custom properties, `--demo-ink`, `--demo-panel`, `--demo-accent` and the rest, are on `#demo-kit`; text is FenrirText's
  embedded default face). `fenrir_add_demo_app` copies them into every app's asset root, but while the source tree is
  there the kit reads them from it (the scaffold's `FENRIR_DEMO_SOURCE_ASSETS`), with hot reload on except in a smoke
  run: edit `demo.css` while a demo runs and the kit restyles; edit `demo.ui` and the kit makes its sections and the
  help again in it. `AddStylesheet(path)` loads a demo's own sheet over `demo.css`. A sheet or document that loads with
  problems is logged as an error, so a smoke run fails on it.

`DemoConfig::demoUITarget` is the tests' seam: a 2D target the kit's root draws into instead of the window.

## The ImGui overlay

`DemoConfig::imguiOverlay = true` installs the scaffold's `ImGuiOverlay` (ADR-0027): Dear ImGui over the main window,
for demo panels. Dear ImGui is the `libs/imgui` submodule (tag v1.92.9b), built here as the static `FenrirDemoImGui`
with the spec's local overrides (`libs/CMakeLists.txt`), and linked only by the scaffold; it reaches a demo app through
the scaffold, never through an engine package. A scene makes ImGui windows from its `update` with ImGui's own functions
(`#include <imgui.h>`): the scaffold starts ImGui's frame in `BeforeTicks` and ends it in `PostUpdate`.

- **Input:** an input router of priority `ImGuiOverlayRouterPriority`. ImGui gets keys, text, the mouse and the wheel,
  and takes what it wants, judged by its previous frame: a click, a press or the wheel over an ImGui window, keys while
  a widget is active (a text field, a slider being dragged), and text while a text field is. A drag that began on ImGui
  is its alone, motion included. Every press ImGui does not take is the game's; ImGui sees it as a click away from its
  windows, so clicking the scene takes the keyboard back from a text field. While the game holds a mouse button (the
  free-fly camera's look) ImGui sees neither the pointer nor the wheel.
- **Input claims,** published each frame: `pointer` (over an ImGui window), `pointerCapture` (an ImGui drag),
  `keyboardFocus` (a widget is active, which cancels the scene's held keys) and `textInput` with the text field's caret,
  where FenrirWindow puts the IME. With the overlay on, the window's text input is on only where ImGui claims it
  (`WindowConfig::textInputByDefault` is false).
- **Drawing:** ImGui's draw data becomes one `DrawList2D` a frame on `builtin:2d`: the ImGui vertex layout, sRGB vertex
  colours, straight `AlphaBlend` and linear blending, with ImGui's clip rectangles as scissors, submitted to the main
  window on overlay layer `ImGuiOverlayLayer` (1000), so a scene's own 2D lists on lower layers draw under it, and debug
  text over it. ImGui's textures, its font atlas among them, are `ImageData` assets owned by the app, replaced whole when
  ImGui changes them. ImGui lays out in logical units, and rasterises its fonts at the window's content scale.
- **Platform:** the clipboard and the cursor shape through FenrirWindow (the cursor is left alone while it is captured).
  Nothing is saved between runs (no `imgui.ini`).

## Keys

| Key | What it does |
| --- | --- |
| PageDown / PageUp | The next / previous scene, wrapping at both ends; the demo panel's scene switcher enters any scene on a click. |
| F1 | Hides or shows the demo UI kit: the demo panel and the help. Showing it also logs the help. |
| F2 | The app's (its `DemoConfig::appKeyRows`, or `appKeyHelp`, say what it does). |
| F3 | Turns the free-fly camera on or off. A scene that spawns an enabled `Camera` of its own starts with the free-fly camera off; F3 then switches between the two. |
| Right mouse button | Held, the mouse looks around (up and down the other way with the `FreeFlyLook` app resource's `invertPitch`), and WASD moves the free-fly camera (by key position), Shift makes it faster and the wheel changes its speed. Without it, WASD and the wheel are the scene's. |
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
- `BoxTextured/BoxTextured.gltf`: Khronos's glTF sample "Box Textured" by Cesium, CC BY 4.0, a cube of side 1 with one
  base colour texture, the Cesium logo (a trademark the licence does not cover); `BoxTextured/attribution.txt` holds
  the attribution and the upstream commit.
- `skybox/skybox.cubemap`: a cubemap of six 512-pixel PNG faces, a procedural sky with hills and a sun, made for the
  engine by `skybox/make_skybox.py` (MIT, as the repository; `skybox/attribution.txt`).
- `skybox-darcy/skybox.cubemap`: the legacy DemoApp's skybox, six 256-pixel PNG faces by Darcy Wilson, CC0
  (`skybox-darcy/attribution.txt`). Its `right`, `left`, `top`, `bottom`, `front` and `back` faces are +X, -X, +Y, -Y,
  +Z and -Z, as the legacy DemoApp mapped them.
- `icons/fenrir.png`: Fenrir's 512-pixel window icon, the legacy DemoApp's, which every demo app's window shows
  (`DemoConfig::iconPath`).

The art-deco texture set comes with the scene that uses it.

## Tests

`FenrirDemoScaffoldTests` (CTest `FenrirDemoScaffold`) runs a demo app on the null backend with no window, its input
fed through FenrirInput's feed and the App driven one frame at a time, or run as a smoke run: the scene list, the free-
fly camera, the scene click, the help, the smoke runs and the ImGui overlay taking input from the scene; and the demo
UI kit, its root drawing into a capturing target the size of the null backend's surface: the help's key rows in order
and a scene's `keyHelp` as one row, F1 hiding the kit (its panel then taking no press) and hiding it from a focused
button, a press on the panel sending no scene click and one beside it sending one, a right-drag beside it turning the
free-fly camera, a scene's section gone after PageDown, a section's heading collapsing and opening it, and the gamepad
reaching the scene while the kit has the focus; and the demo panel's rows (`Tests/DemoPanel.cpp`): a readout set by a
scene's `update` shown that frame and a pulled one following its function, a scene's readouts gone after it is left
and fresh when it is entered again, a readout hidden and shown and one greyed out, a separator's title, the scene switcher listing the
scenes, marking the active one (after a click and after PageDown) and entering the third on a click, and the frame
stats' frame time, fps and the renderer's draws on the null backend.
