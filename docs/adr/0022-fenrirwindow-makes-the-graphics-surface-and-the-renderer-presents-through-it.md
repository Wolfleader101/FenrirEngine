---
status: accepted
---

# FenrirWindow makes the graphics surface, and the renderer presents through it

SDL3 lives only inside FenrirWindow (ADR-0020), and it fixes the graphics API when it creates a window. So FenrirWindow creates each window and the API-specific **graphics surface** SDL makes for it: the OpenGL context today, and later a Vulkan surface, the `HWND` for D3D or a `CAMetalLayer`. The graphics backend owns everything built on top of the surface: an explicit API's swapchain, or for GL the use of the context and its default framebuffer. The renderer decides when to present. FenrirWindow never draws or presents on its own initiative.

## Who knows what

- **FenrirWindow knows graphics APIs only as far as SDL needs.** It sets the API flag and attributes, creates the context or surface, and exposes concrete functions on `Window`, keyed by `WindowId`: request a surface, get a proc address, make current, present, set the swap interval, read the pixel size. Later it adds Vulkan instance extensions, creating a Vulkan surface, the `HWND` and the Metal layer. Its public headers use only neutral types (`void*` proc addresses, integer handles), so no public header includes SDL or GL.
- **The renderer core sees an abstract surface.** The core declares a private interface, `GraphicsSurface`, and the graphics backends use only that. The renderer's ECS facade is the only code that links FenrirWindow (`PRIVATE`). It requests the main window's surface and wraps it in an adapter that implements `GraphicsSurface`. So the core keeps ADR-0019's independence from the app, its tests can use a fake surface, a different windowing library needs only a new adapter, and FenrirWindow never depends on FenrirRenderer.

## Creating the window

- **The window is created when the renderer asks for its surface.** `Window::Install` starts SDL and keeps its `WindowConfig`, but creates no window yet. `Renderer::Install` requests the main window's surface, with:
  - the API from `RendererConfig`'s backend;
  - for GL, a 4.6 core context, a debug context when debug validation is on, and the backbuffer described below.

  FenrirWindow then creates the window and the context. If that fails (no 4.6 context), `Renderer::Install` fails with SDL's message, as ADR-0016 requires. The backend is named once, in `RendererConfig`, never in `WindowConfig`.
- **Without a renderer, or with the `Null` backend**, nothing requests a surface. FenrirWindow then creates a plain window, with no API flag, in the first frame's `PreInit`.
- **The backbuffer is plain sRGB colour**, with no depth, no stencil and no MSAA. Every view draws into offscreen targets (ADR-0019); only `Overlay` and `Output` write the backbuffer. MSAA applies to the scene targets through `RendererConfig`.
- **`WindowConfig::visible`** (default `true`) gives GL tests (ticket 20) and offscreen tools a hidden window, so they make the context the app's way. Present on a hidden window does nothing.

## Each frame

- **Present is the renderer's last step of `Render`**, once per window it drew. Under GL it goes through the surface (`SDL_GL_SwapWindow` inside FenrirWindow); an explicit backend presents its own swapchain. Godot (`DisplayServer::swap_buffers` under GL), Bevy (wgpu's surface texture) and Unreal (`RHIEndDrawingViewport`) split it the same way.
- **Vsync is a renderer setting.** `RendererConfig` has `Off`, `On` and `Adaptive`, changeable at runtime through `Renderer`. The renderer applies it through the surface (the swap interval under GL, the present mode later). If a mode is not supported (`Adaptive` without late-swap tearing), it falls back to `On` and logs it.
- **Size comes from FenrirWindow.** In `PreUpdate`, FenrirWindow updates each window's logical and pixel sizes from SDL's events. At the start of `Render` the renderer reads the pixel size. A change resizes the swapchain (nothing to do under GL) and gives new transient sizes.
- **Minimised windows are skipped.** For a minimised or zero-sized window, the renderer builds, submits and presents nothing, but it still advances the frame index, so deferred destruction keeps going.
- **A minimised app sleeps.** When no window can be drawn, FenrirWindow sleeps `WindowConfig::minimisedFrameSleep` (default about 7 ms, as Godot's `low_processor_usage_mode_sleep_usec`) at the end of `PreUpdate`. Ticks keep running on their accumulator. `unfocusedFrameSleep` (default 0) lets a game throttle while unfocused, as Bevy's `reactive_low_power` and Unreal's `t.IdleWhenNotForeground` do. Pausing a game when it loses focus (Unity's `runInBackground = false`) is game policy, not FenrirWindow's.

## Windows, order and exit

- **One main window.** The API is keyed by `WindowId`, and events name their window (ADR-0017), but the foundation creates and draws to the main window only. Several windows, drawn with one shared GL context made current per window, are a later item.
- **Window installs before Renderer.** `Renderer::Install` requires the `Window` owner whenever its backend needs a surface (any backend but `Null`), and fails with a diagnostic without it.
- **Teardown runs in reverse.** The `Renderer` owner waits for the GPU, frees its device resources and releases its surface. Then FenrirWindow destroys the context. Last, the `Window` owner destroys its windows and quits SDL. The owners are values the app holds, as with `Physics`, so declaring them in install order gets this for free. Destroying a window whose surface is still held is a diagnosed error.
- **Closing.** A close request (the window's ✕) is a FenrirWindow event naming the window. With `WindowConfig::stopOnMainWindowClose` (default `true`), closing the main window calls `App::Stop`. A game that asks "save before quitting?" turns it off and handles the event itself. Windows are destroyed only at teardown, never in the middle of a frame.

## Considered options

- **FenrirWindow hands out a neutral native window and the GL backend makes its own context** with WGL, GLX or EGL. Rejected: platform code per OS for what SDL already does.
- **FenrirRenderer also links SDL `PRIVATE`** and calls `SDL_GL_*` on FenrirWindow's window. Rejected: switching windowing library would mean rewriting the renderer too, against ADR-0020.
- **The renderer core depends on FenrirWindow directly.** Rejected: the core would gain an app-level dependency, and its tests would need a window.
- **The surface interface in a third package** that both depend on. Rejected: it has one implementer, so the core's private interface and an adapter give the same independence for less.
- **The graphics API in `WindowConfig` as well as `RendererConfig`.** Rejected: the same choice made in two places, which can disagree.
- **Spinning while minimised**, or pausing whenever unfocused. Rejected: the first wastes a core; the second is a game's decision.

## When to revisit

When a second window is needed (the editor), when an explicit-API backend is built (the Vulkan, `HWND` and Metal parts of the surface), or when a render thread arrives and the GL context has to move off the main thread (ADR-0020).
