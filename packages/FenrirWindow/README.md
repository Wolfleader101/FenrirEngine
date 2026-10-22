# FenrirWindow

The main window of a Fenrir app, its services and its graphics surface, and the keyboard, mouse and gamepad input it feeds to FenrirInput (ADR-0017, ADR-0020, ADR-0022). It is built over SDL3 3.4.x, which is built static and linked `PRIVATE`. No public header names SDL, so a game never sees it. Built with `FENRIR_ENABLE_RENDERER` (on by default).

```cpp
#include "FenrirWindow/Window.hpp"

Fenrir::App app;
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

The **pump** is a sequential `PreUpdate` system. It takes SDL's events, feeds the input to FenrirInput's input feed, and calls `Input::Dispatch`, so this frame's input reaches `Keyboard`, `Mouse`, `Gamepads` and the input events in this frame, whichever package was installed first. It then reads each window's logical size, pixel size and content scale. When no window can be drawn (all minimised), it sleeps `minimisedFrameSleep` (about 7 ms) at the end of `PreUpdate`; when no window has the focus, it sleeps `unfocusedFrameSleep` (0 by default, so never). Ticks keep running on their accumulator.

## Main-thread rule

SDL's events, windows and GL calls must stay on the main thread (ADR-0020). Every FenrirWindow system is sequential, so it runs on the thread that runs the App. Call `Window`'s functions from that thread too, outside parallel systems.

## WindowConfig

`title`; `logicalSize` (1280 x 720); `visible` (default `true`; `false` gives a hidden window for GL tests and tools, and presenting to it does nothing); `stopOnMainWindowClose` (default `true`); `minimisedFrameSleep` (0.007 s); `unfocusedFrameSleep` (0 s); `textInputByDefault` (default `true`: SDL's text input is on in each window, so typing sends `TextEvent`; ticket 04 makes it follow input routers' `textInput` claims).

## Windows and events

Windows are keyed by FenrirInput's `WindowId`. The foundation makes only the main window, `Window::MainWindow()`, but the API is shaped for several. `Exists`, `LogicalSize`, `PixelSize`, `ContentScale` and `IsMinimised` read a window as of this frame's `PreUpdate`.

Events, read through `EventReader<T>` (ADR-0012), each name their window:

- `WindowResizedEvent`: the logical size, pixel size or content scale changed. It carries the new values.
- `WindowFocusEvent` (FenrirInput's): the window gained or lost the input focus. FenrirWindow feeds focus to the input feed, so `Keyboard::FocusedWindow` agrees.
- `WindowCloseRequestedEvent`: the close button, or a quit signal (Ctrl+C) as the main window's close. With `stopOnMainWindowClose`, closing the main window also calls `App::Stop`. Without it, game code decides, for example to ask "save before quitting?".

## Services

- `SetCursorMode(app, window, mode)`: `Normal`, `Hidden`, or `Captured` (hidden, held in the window, with raw relative motion for mouse look).
- `SetCursorShape(app, window, shape)`: `Arrow`, `Text`, the four resize shapes, `Hand`, `NotAllowed`, or `Hidden`.
- `ClipboardText(app)` and `SetClipboardText(app, text)`: the system clipboard, as UTF-8.
- `SetTitle(app, window, title)`.

A title, cursor mode or cursor shape set before the window is made is applied when it is made.

## The graphics surface

The renderer's adapter makes and uses each window's graphics surface through static functions keyed by `WindowId`, with neutral types only (ADR-0022): `RequestSurface` (API, debug context, backbuffer description; for OpenGL, a 4.6 core context), `ReleaseSurface`, `HasSurface`, `GetProcAddress` (a `void*`), `MakeCurrent`, `Present`, `SetSwapInterval` (0 off, 1 vsync, -1 adaptive) and `PixelSize`. A surface must be requested before the first frame: a window made without one cannot gain one. `Present` on a hidden window does nothing.

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
