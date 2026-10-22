# 03: FenrirWindow over SDL3: the main window, its services and the input feed

**What to build:** A game can open a window, read real keyboard, mouse and gamepad input through FenrirInput, and stop when the window closes, with SDL3 named by no public header (spec: "FenrirWindow", "Build" (SDL3 row, root options, CI and README, `PublicHeaders.cpp`); ADR-0020, ADR-0022, ADR-0017). The graphics surface functions are built here and first used by ticket 10.

- **SDL3 3.4.x** as a pinned submodule at `packages/FenrirWindow/libs/SDL`, static and `PRIVATE`, with the spec's local overrides (cleared directory `COMPILE_OPTIONS`, warnings-as-errors off and `SYSTEM`, `BUILD_SHARED_LIBS OFF`, dynamic MSVC runtime, no LTO). Built subsystems: video (with clipboard), events, keyboard, mouse, text input and IME, joystick, gamepad, haptic, hidapi and sensor. Off: audio, GPU, 2D render, camera, dialogs, tray, process, `SDL_main` (`SDL_MAIN_HANDLED`, `SDL_SetMainReady`), and SDL's tests, examples and install. The ticket picks the exact tag.
- **Root option** `FENRIR_ENABLE_RENDERER` (`ON`) introduced here, building FenrirWindow and SDL3 (later tickets add FenrirRenderer, the shader compiler, glad and glslang under it). With it off, no window target exists. The README options table gains it; CI's `FENRIR_CMAKE_OPTIONS` turns it on; Linux CI installs the X11 and Wayland development packages SDL3's video needs.
- **`Window::Install(App&, WindowConfig)`** starts SDL's video subsystem, installs FenrirInput if it is not there yet, and keeps its config, but creates no window. Without a renderer requesting a surface, a plain window with no API flag is made in the first frame's `PreInit`. When SDL's video cannot start it logs and returns an owner with `IsValid() == false` (ticket 01's shape).
- **`WindowConfig`:** title, logical size, `visible` (default `true`), `stopOnMainWindowClose` (default `true`), `minimisedFrameSleep` (about 7 ms), `unfocusedFrameSleep` (default 0), `textInputByDefault` (default `true`).
- **One main window** keyed by `WindowId`, the API shaped for several.
- **Each frame:** pump SDL at the start of `PreUpdate` (sequential), feed FenrirInput, call `Input::Dispatch(app)`, and update each window's logical size, pixel size and content scale.
- **Same-frame input whatever the install order** (settled with the user on 2026-10-22, found while building ticket 02): FenrirInput gains a public `Input::Dispatch(App&)` that processes everything fed since the last call and does nothing when nothing is pending. FenrirInput's own sequential `PreUpdate` system still calls it, and FenrirWindow's pump calls it right after feeding, so input fed by the pump reaches `Keyboard`, `Mouse`, `Gamepads` and game readers in the same frame whether Input or Window was installed first. Routers (ticket 04) still see one `BeginDispatch` per frame, before the frame's first event. Sleep at the end of `PreUpdate` when nothing can draw (minimised; unfocused when configured).
- **Services:** cursor modes (normal, hidden, captured with relative motion); `SetCursorShape` (arrow, text, the four resize shapes, hand, not-allowed, hidden); clipboard text get and set; `SetTitle`; focus events fed to FenrirInput; SDL text input on per window following `textInputByDefault` (ticket 04 makes it follow `textInput` claims).
- **Events:** resized, focus changed and close requested, each naming the window. With `stopOnMainWindowClose`, closing the main window calls `App::Stop`; without it, game code handles the event.
- **Surface functions** on `Window`, statics taking `App&` and keyed by `WindowId`, with neutral types only (`void*` proc addresses, integer handles): request a surface (API, debug context, backbuffer description), get a proc address, make current, present, set the swap interval, read the pixel size. Destroying a window whose surface is still held is a diagnosed error. Windows are destroyed only at teardown.
- **`PublicHeaders.cpp`** fails the build if SDL reaches a public FenrirWindow header, as physics does for Jolt.
- README for the package: install order (before the renderer, torn down after it), the main-thread rule, and the accepted freeze while dragging on Windows.

**Blocked by:** 02 (FenrirInput: the input feed, Keyboard, Mouse and Gamepads, and input events).

**Touches:** `packages/FenrirWindow/`, `packages/FenrirInput/`, `.gitmodules`, `CMakeLists.txt`, `README.md`, `.github/workflows/build-and-test.yml`

**Status:** resolved

- [x] `FenrirWindowTests`: a hidden window is created, resized (resize event, new logical and pixel sizes) and closed (close-requested event; `App::Stop` with `stopOnMainWindowClose`, not without); the tests report skipped (CTest `SKIP_RETURN_CODE`) without a display.
- [x] Clipboard text round-trips; `SetTitle` and cursor modes do not error on a hidden window.
- [x] No public FenrirWindow header names or includes SDL, and SDL's include path reaches no target outside FenrirWindow.
- [x] `FENRIR_ENABLE_RENDERER=OFF` configures and builds the engine without FenrirWindow or SDL3; Clang and MSVC x64 debug and release presets build with no warnings from SDL's sources; Linux CI builds it.
- [x] A small manual check: a visible window opens, keys and mouse reach `Keyboard`/`Mouse`, a gamepad hot-plugs, minimising sleeps, closing stops the app (checked by hand).
