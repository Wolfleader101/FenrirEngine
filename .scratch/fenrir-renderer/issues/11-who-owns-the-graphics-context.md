# Who owns the graphics context and the swapchain or surface?

Type: grilling
Status: resolved
Blocked by: 08, 09

## Question

Who creates and owns the OpenGL context today, and the swapchain or surface a Vulkan, D3D or Metal backend would need later? **ADR.**

- Does FenrirWindow know about graphics APIs at all (creating a GL context, a Vulkan surface), or does it hand out a neutral native window and let the graphics backend make the context or surface?
- Who presents (swaps buffers), and who handles resizing, minimising and vsync?
- Is there one window or several, and can the renderer draw to more than one?
- How does `Install` order go (window before renderer), and who tears down what at exit?

## Answer

ADR-0022: **FenrirWindow makes the graphics surface** (the window with its API flag, the GL context, later the Vulkan surface, `HWND` or Metal layer), and **the graphics backend owns what is built on it** (an explicit API's swapchain, or for GL the use of the context).

- **Seam:** FenrirWindow exposes concrete surface functions on `Window`, keyed by `WindowId`, with neutral types only. The renderer core declares a private abstract `GraphicsSurface` that the backends use. Only the ECS facade links FenrirWindow (`PRIVATE`) and adapts the main window's surface to `GraphicsSurface`, so the core tests with a fake surface and FenrirWindow never depends on the renderer.
- **Creation:** `Window::Install` starts SDL but creates no window. `Renderer::Install` requests the main window's surface (API from `RendererConfig`; for GL a 4.6 core context, a debug context when validation is on, an sRGB colour backbuffer with no depth, stencil or MSAA), and FenrirWindow creates the window and context then. If that fails, `Install` fails with SDL's message. With no renderer or the `Null` backend, a plain window is created in the first frame's `PreInit`. `WindowConfig::visible` gives tests a hidden window.
- **Present:** the renderer's last step of `Render`, once per window drawn, through the surface under GL or its own swapchain later.
- **Vsync:** `RendererConfig` `Off`/`On`/`Adaptive`, changeable at runtime. A mode that isn't supported falls back to `On` and logs it.
- **Resize and minimise:** FenrirWindow updates logical and pixel sizes in `PreUpdate`. The renderer reads the pixel size each `Render` and skips minimised or zero-sized windows while still advancing the frame index. FenrirWindow sleeps `minimisedFrameSleep` (about 7 ms, as Godot) when nothing can draw, and `unfocusedFrameSleep` (default 0) is optional (Bevy, Unreal). Pausing on focus loss (Unity) is game policy.
- **Windows:** one main window in the foundation, the API shaped for several. Several windows with one shared GL context are a later item.
- **Order and exit:** Window installs before Renderer and is torn down after it. The renderer waits for the GPU, frees its resources and releases its surface; then FenrirWindow destroys the context, the windows and SDL. Destroying a window whose surface is held is a diagnosed error. A close request is an event naming the window, and `stopOnMainWindowClose` (default `true`) calls `App::Stop`.
