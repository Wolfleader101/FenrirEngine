---
status: accepted
---

# FenrirWindow uses SDL3, and OpenGL stays on the main thread

FenrirWindow needs a windowing library underneath: windows, the OpenGL context today, a Vulkan, D3D or Metal surface later, and the input it feeds to FenrirInput. GLFW 3.4 and SDL3 are equal on windowing basics (branch `research/glfw-or-sdl3`). FenrirWindow uses **SDL3**, pinned to 3.4.x, built static and linked `PRIVATE`. No public header names it, as ADR-0010 asks, so callers never see the choice.

## Why SDL3

- **What GLFW lacks can only be written by us.** IME composition (which the user's GUI library will want), gamepad rumble, gyro sensors, touch and pen are still open upstream in GLFW. Filling them means platform code per OS.
- **What SDL3 costs is paid once.** It is about 16 times GLFW's size and build time (8–14 MB, 6–36 s against 0.6 MB, 1–2 s). CMake options cut the parts we do not use.
- **Later backends get their surfaces cleanly.** SDL3 gives a `HWND` without `<windows.h>` and a `CAMetalLayer`. GLFW needs `glfw3native.h` and gives only the `NSView`.

## What we use and leave off

- **Built:** video (windows, the GL context, Vulkan and Metal surface support, clipboard), events, keyboard, mouse, text input and IME, joystick, gamepad, haptic, hidapi and sensor.
- **Off:** audio, the GPU API, the 2D render API, camera, dialogs, tray, process, and `SDL_main`. The engine keeps its own `main` (`SDL_MAIN_HANDLED` and `SDL_SetMainReady`). Other subsystems are turned on when something needs them.

## What we accept

- **OpenGL calls stay on the main thread.** SDL3 marks every `SDL_GL_*` call (make-current, swap) main-thread only. ADR-0016 already sends every GPU call through one thread; with SDL3, under OpenGL, that thread is the main thread. A render thread waits for an explicit-API backend, which has no such rule.
- **A window freezes while it is dragged or resized on Windows**, as with either library: the event pump blocks. Live redraw during a drag (SDL3's exposed event from an event watch) is a later item, not in the first version.

## Considered options

- **GLFW 3.4.** Rejected: small and quick to build, and it allows GL calls on any thread. But IME, rumble, sensors, touch and pen would be ours to write per platform.
- **Our own platform layer.** Rejected: the most code for no gain while one library covers everything we need.

## When to revisit

Switch to GLFW or our own platform layer if:

- a GL render thread becomes necessary before an explicit-API backend exists;
- SDL3's build time or size hurts CI or iteration even with its options off;
- SDL3 upstream breaks something we depend on.

Because SDL3 is hidden, a switch rewrites FenrirWindow's internals and FenrirInput's feed from it, not their callers.
