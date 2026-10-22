# What do GLFW 3.4 and SDL3 each offer FenrirWindow?

Type: research
Status: resolved
Blocked by: none

## Question

What do GLFW (3.4) and SDL3 each offer a windowing package that must hide which of them it uses, and that must later give a Vulkan, D3D or Metal backend its surface?

- **Windows:** creation, several windows, fullscreen and borderless modes, resizing, minimising, high DPI and content scale, the cursor (hidden, captured, relative motion).
- **Graphics contexts and surfaces:** creating an OpenGL context and swapping buffers; giving a Vulkan surface, a Win32 `HWND` (D3D) or a `CAMetalLayer`; vsync and swap interval. Whether a library can make a window with no API attached.
- **Input:** keyboard (key codes and scancodes, layouts), text input and IME, mouse, gamepads (mapping database, hot plug, rumble), touch and pen; whether input comes as callbacks, polled state or an event queue, and on which thread.
- **The loop:** event pumping, what blocks (resizing and moving on Windows), threading rules (which calls must be on the main thread).
- **Beyond windowing:** SDL3's GPU API, audio, file dialogs, clipboard; what it costs to build them in or leave them out.
- **Build:** CMake as a submodule, static linking, options to turn off, licence, size, and warnings under clang `-Weverything` and MSVC `/W4`.

Write findings to a Markdown file on the `research/glfw-or-sdl3` branch (`.scratch/fenrir-renderer/research/glfw-or-sdl3.md`), citing each claim's source and pinning the versions read, and link it here.

## Answer

Findings: `.scratch/fenrir-renderer/research/glfw-or-sdl3.md` on branch `research/glfw-or-sdl3` (commit `a6c1e44`), against GLFW 3.4 (`7b6aead`; 3.5.1's notes checked) and SDL 3.4.18 (`829a65d`), each claim cited.

- **Windowing basics are equal:** several windows, fullscreen and borderless, DPI, cursor modes, GL contexts with swap interval, Vulkan surfaces, `HWND`.
- **Graphics API fixed at window creation in both** (`GLFW_NO_API`; SDL's `OPENGL`/`VULKAN`/`METAL` flags), so FenrirWindow must know the backend before making a window.
- **Later surfaces:** SDL3 gives `HWND` without `<windows.h>` and a `CAMetalLayer`; GLFW needs `glfw3native.h` and only gives the `NSView`.
- **Input:** GLFW is callbacks plus polled state; SDL3 an event queue plus polled state. SDL3 adds IME, rumble, sensors, touch and pen, still open upstream in GLFW; both ship SDL_GameControllerDB with hot plug.
- **Threading:** pumping and window creation main-thread only in both; GLFW allows GL make-current and swap from any thread, SDL3 marks every `SDL_GL_*` main-thread only.
- **Dragging a window blocks the pump on Windows in both;** SDL3 gives a timer-driven exposed event to render from, GLFW only refresh and size callbacks.
- **SDL3 extras** (GPU API with no GL backend, audio, dialogs) are CMake options; disabling most cut its static library from 9.8 to 8.1 MB.
- **Build:** both zlib, both submodule-friendly; SDL needs `SDL_STATIC=ON`. GLFW ~36k lines, 0.6 MB, 1–2 s build; SDL ~575k lines, 8–14 MB, 6–36 s.
- **Warnings:** the root `add_compile_options` reaches both (thousands of clang `-Weverything` warnings, no failure); public headers are clean once included as `SYSTEM`.
