# Which library sits behind FenrirWindow, GLFW or SDL3?

Type: grilling
Status: resolved
Blocked by: 03

## Question

Which windowing library does FenrirWindow use, given that the choice must not show through its interface? What do we use of it, what do we leave off in the build, and what would make us switch?

## Answer

ADR-0020: **SDL3**, pinned to 3.4.x, built static and linked `PRIVATE`, named by no public header.

- **Why:** what GLFW lacks (IME, rumble, sensors, touch, pen) would be ours to write per platform; SDL3's cost (about 16 times GLFW's size and build time) is paid once and cut by options. It also gives later backends a `HWND` without `<windows.h>` and a `CAMetalLayer`.
- **Built:** video (with clipboard), events, keyboard, mouse, text input and IME, joystick, gamepad, haptic, hidapi, sensor. **Off:** audio, GPU, 2D render, camera, dialogs, tray, process, `SDL_main` (the engine keeps its own `main`). Others are turned on when needed; the option names are ticket 21's.
- **Accepted:** SDL3 makes every `SDL_GL_*` call main-thread only, so under OpenGL the one GPU thread of ADR-0016 is the main thread; a render thread waits for an explicit-API backend. A window freezes while dragged or resized on Windows; live redraw during drags is later.
- **Switch** to GLFW or our own layer if a GL render thread is needed before an explicit backend, if SDL3's build cost hurts CI or iteration with options off, or if upstream breaks something we use.

