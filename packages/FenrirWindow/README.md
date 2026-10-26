# FenrirWindow

The main window of a Fenrir app, its services and its graphics surface, and the keyboard, mouse and gamepad input it feeds to FenrirInput (ADR-0017, ADR-0020, ADR-0022). It is built over SDL3 3.4.x, which is built static and linked `PRIVATE`. No public header names SDL, so a game never sees it. Built with `FENRIR_ENABLE_RENDERER` (on by default).

```cpp
#include "FenrirWindow/Window.hpp"

Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
Fenrir::WindowConfig config;
config.title = "My game";
Fenrir::Window window = Fenrir::Window::Install(app, config); // before the Renderer; keep it alive while the app runs

app.AddSystem(Fenrir::SchedulePriority::Update, [](Fenrir::App& app) {
    if (app.GetResource<Fenrir::Keyboard>().JustPressed(Fenrir::Key::Escape)) { app.Stop(); }
});
app.Run(); // closing the main window stops it too
```

## Install and order

- `Window::Install(App&, WindowConfig)` starts SDL's video and gamepads, keeps the config, and installs FenrirInput if the app has none yet (or shares it). It makes no window. If a renderer requests a surface for the main window before the first frame, the window is made then, with that surface. Otherwise a plain window with no graphics API is made in the first frame's `PreInit`. Install it before the first frame.
- If SDL's video cannot start (no display), or the app already has a `Window`, `Install` logs why and returns an owner whose `IsValid()` is false. It adds no systems, and FenrirWindow's functions do nothing for that app.
- **Install order: before the Renderer, torn down after it.** Declare the owners in install order (`App`, then `Window`, then `Renderer`) and they are destroyed in reverse. Destroying the `Window` destroys its windows and quits SDL. If a window's graphics surface is still held then, that is a logged error.
- Windows are destroyed only at teardown, never in the middle of a frame.

## Each frame

The **pump** is a sequential `PreUpdate` system. It takes SDL's events, feeds the input to FenrirInput's input feed, and calls `Input::Dispatch`, so this frame's input reaches `Keyboard`, `Mouse`, `Gamepads` and the input events in this frame, whichever package was installed first. After that dispatch, it puts each window's SDL text input as the input routers' claims say (below). It then reads each window's logical size, pixel size and content scale. When no window can be drawn (all minimised), it sleeps `minimisedFrameSleep` (about 7 ms) at the end of `PreUpdate`; when no window has the focus, it sleeps `unfocusedFrameSleep` (0 by default, so never). Ticks keep running on their accumulator.

## Main-thread rule

SDL's events, windows and GL calls must stay on the main thread (ADR-0020). Every FenrirWindow system is sequential, so it runs on the thread that runs the App. Call `Window`'s functions from that thread too, outside parallel systems.

## WindowConfig

`title`; `logicalSize` (1280 x 720); `visible` (default `true`; `false` gives a hidden window for GL tests and tools, and presenting to it does nothing); `stopOnMainWindowClose` (default `true`); `minimisedFrameSleep` (0.007 s); `unfocusedFrameSleep` (0 s); `textInputByDefault` (default `true`: SDL's text input is on in each window no router claims `textInput` in, so typing sends `TextEvent`; a UI app sets it to `false`).

## Text input

SDL's text input follows the input routers' `textInput` claims (ADR-0027). In each window where a router claims `textInput` (FenrirInput's `InputClaims`), the pump keeps it on, with the claimant's caret rectangle as SDL's text input area, where the IME places its candidates. In other windows it follows `textInputByDefault`. A claim published during a frame takes effect in the next frame's pump.

## Windows and events

Windows are keyed by FenrirInput's `WindowId`. The foundation makes only the main window, `Window::MainWindow()`, but the API is shaped for several. `Exists`, `LogicalSize`, `PixelSize`, `ContentScale`, `IsMinimised` and `IsFocused` read a window as of this frame's `PreUpdate`.

Events, read through `EventReader<T>` (ADR-0012), each name their window:

- `WindowResizedEvent`: the logical size, pixel size or content scale changed. It carries the new values.
- `WindowFocusEvent` (FenrirInput's): the window gained or lost the input focus. FenrirWindow feeds focus to the input feed, so `Keyboard::FocusedWindow` agrees.
- `WindowCloseRequestedEvent`: the close button, `RequestClose(app, window)`, or a quit signal (Ctrl+C) as the main window's close. With `stopOnMainWindowClose`, closing the main window also calls `App::Stop`. Without it, game code decides, for example to ask "save before quitting?".

## Services

- `SetCursorMode(app, window, mode)`: `Normal`, `Hidden`, or `Captured` (hidden, held in the window, with raw relative motion for mouse look). `GetCursorMode` reads it back.
- `SetCursorShape(app, window, shape)`: `Arrow`, `Text`, the four resize shapes, `Hand`, `NotAllowed`, or `Hidden`.
- `ClipboardText(app)` and `SetClipboardText(app, text)`: the system clipboard, as UTF-8.
- `SetTitle(app, window, title)`.
- `SetIcon(app, window, width, height, rgba8)`: the icon in the window's title bar and the taskbar, from `width *
  height` RGBA8 pixels (four bytes each, rows top to bottom; FenrirAssets' `ImageData` pixels as loaded from a PNG).
  FenrirWindow keeps a copy. It returns false, with one logged error, for a window it does not keep, an empty size,
  pixels of the wrong size, or when SDL refuses.
- `RequestClose(app, window)`: does what the window's close button does (the event, and `App::Stop` for the main window with `stopOnMainWindowClose`). False for a window not made.

A title, icon, cursor mode or cursor shape set before the window is made is applied when it is made.

## The graphics surface

The renderer's adapter makes and uses each window's graphics surface through static functions keyed by `WindowId`, with neutral types only (ADR-0022): `RequestSurface` (API, debug context, backbuffer description; for OpenGL, a 4.6 core context), `ReleaseSurface`, `HasSurface`, `GetProcAddress` (a `void*`), `MakeCurrent`, `Present`, `SetSwapInterval` (0 off, 1 vsync, -1 adaptive) and `PixelSize`. A surface must be requested before the first frame: a window made without one cannot gain one. `Present` on a hidden window does nothing.

## Scripting

With `FENRIR_ENABLE_SCRIPTING` on as well, the `FenrirWindowScripting` library gives C# the window (ADR-0029): add `Fenrir::Window::ScriptingExtension()` to `ScriptingConfig::extensions`, after `Fenrir::Input::ScriptingExtension()`, whose `Fenrir.Input` contract the window's uses (for `WindowId` and `WindowFocusEvent`). Call `fenrir_deploy_window_scripting(<target>)` beside `fenrir_deploy_scripting` and `fenrir_deploy_input_scripting`. The `Fenrir.Window` contract (`Managed/Fenrir.Window`) has:

- `Window.Main`, and by `WindowId`: `Exists`, `Size`, `PixelSize` (as `WindowSize`), `ContentScale`, `IsMinimised`, `IsFocused`;
- `GetCursorMode` and `SetCursorMode`, `SetCursorShape`, `SetTitle`, `RequestClose`;
- `GetClipboard(Span<char>)`, which fills the span and returns the text's whole length in chars, and `SetClipboard`;
- `WindowResizedEvent` and `WindowCloseRequestedEvent` as native events; focus changes are `Fenrir.Input.WindowFocusEvent`.

The extension needs no `Window` owner, so scripts always load. With no window (no `Window` installed, or the window not made yet) or an id that names none, every function does nothing and returns `false`, zero, an empty size or text, or `CursorMode.Normal`; the clipboard needs the main window. A debug build warns once per native call site under the native's name (`WindowNative.SetTitle: the app has no such window ...`), through a native guard over the extension's state. Inside a `Fenrir.*` namespace, `Window` names the `Fenrir.Window` namespace, so write `Fenrir.Window.Window` there. These rules are tested from C++ with no .NET, as `FenrirWindowScriptingRules` (and `FenrirWindowScriptingRules-Display` for the clipboard; skipped without a display); what a script sees is `FenrirWindowScriptingTests`, run as `FenrirWindowScripting` and `FenrirWindowScripting-Display` (a hidden window; skipped without a display).

The binding is generated (ADR-0043): the events, `WindowSize` and the cursor enums are written once, in this package's headers (`api/FenrirWindow.api.json`), and each native function once, as its definition in `Scripting/src/WindowScripting.cpp`. `Scripting/api/FenrirWindow.binding.toml` generates from them the contract's mirrors and `WindowNative` (`Managed/Fenrir.Window/Generated`), the Iris mappings, the extension's lists and the defaults test, naming `WindowId` as FenrirInput's binding does (`uses`). The contract's own `Window.cs` holds the facade and `WindowSize`'s behaviour. After changing a shared type or a native, run `fenrir-describe-api` and then `fenrir-generate-bindings`.

## Accepted for now

- On Windows, a window freezes while it is dragged or resized, because SDL's event pump blocks. Live redraw while dragging is a later item.
- One main window. Several windows, IME composition, rumble, touch and pen are out of scope.

## SDL3

The submodule is `libs/SDL`, pinned to `release-3.4.18`. `libs/CMakeLists.txt` builds it static with every override local to that directory. It clears the engine's warning flags and silences SDL's own, marks SDL `SYSTEM`, and uses the dynamic MSVC runtime with no LTO. It builds video (with clipboard, keyboard, mouse, text input and IME), events, joystick, gamepad, haptic, hidapi and sensor. It turns off audio, GPU, 2D render, camera, dialogs, tray and power, and SDL's tests, examples and install. SDL 3.4 has no switch for its process API, so that is compiled but never called. `SDL_main` is not used: the engine keeps its own `main` (`SDL_MAIN_HANDLED`, `SDL_SetMainReady`). Debug and Release are supported. On Linux, SDL's video needs the X11 and Wayland development packages (see the CI workflow).

## Tests

`FenrirWindowTests` (with `-DFENRIR_BUILD_TESTS=ON`) runs as three CTest tests:

- `FenrirWindow`: needs no display. `PublicHeaders.cpp` fails the build if SDL reaches a public header or the test target.
- `FenrirWindow-Display`: a hidden main window is made, resized and closed, with the services, the surface functions and same-frame input. Without a display these report skipped, never passed.
- `FenrirWindow-NoVideo`: `Install` when SDL's video cannot start.

The tests reach SDL only through `WindowProbe`, a test library that pushes SDL events and resizes windows. Making a GL context and presenting through it are first tested by the renderer's GL tests.
