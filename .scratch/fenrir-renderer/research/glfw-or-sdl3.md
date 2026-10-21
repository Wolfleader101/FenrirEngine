# What GLFW 3.4 and SDL3 each offer FenrirWindow

Research for [03-glfw-or-sdl3](../issues/03-glfw-or-sdl3.md). This file reports what each library offers a windowing package that hides which one it uses (ADR-0010) and must later hand a Vulkan, D3D or Metal backend its surface. It does not choose between them.

Sources are each library's source tree, pinned to the commit read, plus their in-tree docs and issue trackers. Line numbers refer to these commits:

- GLFW tag `3.4` @ [`7b6aead`](https://github.com/glfw/glfw/tree/7b6aead9fb88b3623e3b3725ebb42670cbe4c579) (released 2024-02-23). Links below use the prefix `GL/` for `https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/`.
- GLFW tag `3.5.1` @ [`d9d6f0f`](https://github.com/glfw/glfw/tree/d9d6f0f1f967807ffade6598ea9a631ebaf37a56) (released 2026-07-31), read only for what changed since 3.4.
- SDL tag `release-3.4.18` @ [`829a65d`](https://github.com/libsdl-org/SDL/tree/829a65d769d935c4852f8159e964312c0957260a) (released 2026-10-02, the latest SDL3 release when read). Links below use the prefix `SDL/` for `https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/`.
- Build and warning numbers come from building both trees on this machine (Windows 11, clang 19.1.5, MSVC 19.44.35220, Ninja, Release) with the exact warning flags of FenrirEngine's root `CMakeLists.txt`. The probe is described under [Build](#build).

Every link to GLFW or SDL in this file is a full URL. The `GL/` and `SDL/` shorthands in link text are for reading only.

**Versions in brief.** The ticket names GLFW 3.4. GLFW has since shipped 3.5.1 (there was no 3.5.0). Its release notes list only unlimited mouse buttons, two native-access getters (`EGLConfig`, `GLXFBConfig`) and the removal of Windows XP/Vista and original MinGW ([3.5.1 `docs/news.md` L1–L70](https://github.com/glfw/glfw/blob/d9d6f0f1f967807ffade6598ea9a631ebaf37a56/docs/news.md#L1-L70)). Nothing below changes between 3.4 and 3.5.1. SDL3's stable API dates from 3.2.0 (every function read is marked "available since SDL 3.2.0"); 3.4.x is the current stable line.

---

## Windows

| | GLFW 3.4 | SDL3 (3.4.18) |
|---|---|---|
| Create | `glfwCreateWindow(w, h, title, monitor, share)` after `glfwWindowHint`s ([GL/include/GLFW/glfw3.h L3053, L3235](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L3235)) | `SDL_CreateWindow(title, w, h, flags)` with a 64-bit flag set ([SDL/include/SDL3/SDL_video.h L197–L220, L1184](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L1184)) |
| Several windows | Yes; each `GLFWwindow*` has its own callbacks and (optionally) context, and `share` shares GL objects | Yes; events carry a `windowID` |
| Exclusive fullscreen | `glfwSetWindowMonitor(window, monitor, x, y, w, h, refresh)` ([L4074](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4074)) | `SDL_SetWindowFullscreenMode(window, mode)` with a mode from `SDL_GetFullscreenDisplayModes()`, then `SDL_SetWindowFullscreen(window, true)` ([SDL_video.h L1005–L1034, L2360](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L1005-L1034)) |
| Borderless fullscreen | No dedicated call: pass the monitor's *current* video mode to `glfwSetWindowMonitor` and GLFW makes a "windowed full screen" window; you must have saved the desktop mode yourself if the monitor is already in another mode ([GL/docs/window.md L94–L123](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/window.md#L94-L123)) | `SDL_SetWindowFullscreenMode(window, NULL)` means borderless fullscreen desktop ([SDL_video.h L1019–L1022](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L1019-L1022)) |
| Borderless window | `GLFW_DECORATED` hint/attribute ([L882](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L882), `glfwSetWindowAttrib` [L4153](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4153)) | `SDL_WINDOW_BORDERLESS` ([SDL_video.h L201](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L201)) |
| Resize | Window-size and framebuffer-size callbacks; `glfwGetFramebufferSize` ([L3663, L4465](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4465)) | `SDL_EVENT_WINDOW_RESIZED` and `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` events; `SDL_GetWindowSizeInPixels` ([SDL_events.h L143](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L143), [SDL_video.h L2013](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L2013)) |
| Minimise | `glfwIconifyWindow` and an iconify callback ([L3824, L4405](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4405)) | `SDL_MinimizeWindow` and `SDL_EVENT_WINDOW_MINIMIZED` ([SDL_video.h L2298](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L2298), [SDL_events.h L145](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L145)) |
| Fullscreen changes async? | Not documented as async | On some window systems the request is asynchronous; `SDL_SyncWindow` blocks until it lands ([SDL_video.h L1008–L1012, L2391](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L1008-L1012)) |

### High DPI and content scale

- **GLFW** reports a content scale per window (`glfwGetWindowContentScale`, [L3733](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L3733)) with a change callback ([L4496](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4496)), and the framebuffer size separately from the window size. Two hints decide behaviour: `GLFW_SCALE_TO_MONITOR` resizes the window by the content scale, and `GLFW_SCALE_FRAMEBUFFER` (new in 3.4) asks for a full-resolution framebuffer on macOS and Wayland ([L1100, L1104](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L1100-L1104); [GL/docs/news.md L70](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/news.md#L70)).
- **SDL3** separates three numbers: the window size, the pixel size (their ratio is the *pixel density*, `SDL_GetWindowPixelDensity`) and the *display scale* (`SDL_GetWindowDisplayScale`, pixel density times the desktop content scale). It sends `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` and `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED`. `SDL_WINDOW_HIGH_PIXEL_DENSITY` asks for a full-resolution back buffer. Its README tabulates the Windows-vs-macOS difference: on Windows window coordinates are pixels and only the scale changes; on macOS (and Wayland) coordinates are points and the flag buys extra pixels ([SDL/docs/README-highdpi.md](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/docs/README-highdpi.md); [SDL_video.h L975, L999](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L975-L999)).

Both expose the same two facts a renderer needs (pixel size for the swapchain, a scale for UI). SDL names and documents the split more explicitly.

### Cursor

| | GLFW 3.4 | SDL3 |
|---|---|---|
| Hidden | `GLFW_CURSOR_HIDDEN`: invisible over the content area, not confined | `SDL_HideCursor()` ([SDL_mouse.h L790](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_mouse.h#L790)) |
| Confined, visible | `GLFW_CURSOR_CAPTURED` (new in 3.4) | `SDL_SetWindowMouseGrab`, or `SDL_SetWindowMouseRect` for a sub-rectangle ([SDL_video.h L2600, L2667](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L2600-L2667)) |
| Relative / mouse-look | `GLFW_CURSOR_DISABLED`: hides, grabs and gives "virtual and unlimited cursor movement" | `SDL_SetWindowRelativeMouseMode(window, true)`: hides, grabs, reports relative motion ([SDL_mouse.h L36, L476](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_mouse.h#L476)) |
| Raw (unaccelerated) motion | `GLFW_RAW_MOUSE_MOTION`, only while disabled, if `glfwRawMouseMotionSupported()` ([L1156, L4767](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4767); [GL/docs/input.md L339–L360](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/input.md#L339-L360)) | Relative mode deltas arrive as `xrel`/`yrel` floats in `SDL_MouseMotionEvent` ([SDL_events.h L455–L463](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L455-L463)), or summed with `SDL_GetRelativeMouseState` ([SDL_mouse.h L382](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_mouse.h#L382)) |

GLFW cursor modes: [GL/include/GLFW/glfw3.h L4680–L4692](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4680-L4692); 3.4 note: [GL/docs/news.md L52](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/news.md#L52).

---

## Graphics contexts and surfaces

### OpenGL

- **GLFW** creates a context with the window by default (`GLFW_CLIENT_API` hint, [L1031](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L1031)). `glfwMakeContextCurrent`, `glfwSwapBuffers`, `glfwSwapInterval` ([L6109, L6164, L6210](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L6109-L6210)). These three, `glfwGetCurrentContext`, `glfwExtensionSupported` and `glfwGetProcAddress` **may be called from any thread** ([GL/docs/intro.md L487–L496](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/intro.md#L487-L496)), so a render thread can own the context. Context creation itself is main-thread only ([L463–L465](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/intro.md#L463-L465)).
- **SDL3** needs `SDL_WINDOW_OPENGL` on the window or `SDL_GL_CreateContext` fails ([SDL_video.h L3272–L3273](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L3272-L3273)). `SDL_GL_MakeCurrent`, `SDL_GL_SetSwapInterval`, `SDL_GL_SwapWindow` ([L3312, L3430, L3471](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L3430-L3471)). Every `SDL_GL_*` function in that header, swap and make-current included, is documented "should only be called on the main thread" ([SDL_video.h L3287–L3471](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L3287-L3471)). So a render thread that owns a GL context is outside SDL's documented contract; GLFW documents it as allowed.
- Swap interval: both take an int, and both accept a negative value for adaptive vsync where the driver supports it: GLFW when the context has `WGL_EXT_swap_control_tear` or `GLX_EXT_swap_control_tear` ([L6174–L6176](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L6174-L6176)), SDL with `-1` ([SDL_video.h L3404–L3420](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L3404-L3420)). Vsync for Vulkan, D3D and Metal is the backend's present mode, outside both libraries.

### A window with no API attached

- **GLFW:** `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)` ([L1140](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L1140); [GL/docs/vulkan.md L223](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/vulkan.md#L223)). This is required for Vulkan: `glfwCreateWindowSurface` fails with `GLFW_INVALID_VALUE` if the window has a GL context ([L6458–L6461](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L6458-L6461)). The client API is fixed at creation; changing it means destroying and recreating the window.
- **SDL3:** omit `SDL_WINDOW_OPENGL`, `SDL_WINDOW_VULKAN` and `SDL_WINDOW_METAL`. The flags are what loads the API: with `SDL_WINDOW_OPENGL`/`SDL_WINDOW_VULKAN` SDL calls the matching `LoadLibrary` and creation fails if there is no Vulkan driver; with `SDL_WINDOW_METAL` creation fails on an OS without Metal ([SDL_video.h L1149–L1158](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L1149-L1158)). `SDL_Vulkan_CreateSurface` requires the window to have been created with `SDL_WINDOW_VULKAN` ([SDL_vulkan.h L205–L207](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_vulkan.h#L205-L226)). So with either library, **the graphics API must be known when the window is created**: FenrirWindow needs that choice as input from whoever picks the backend.

### Vulkan surface

| | GLFW | SDL3 |
|---|---|---|
| Instance extensions | `glfwGetRequiredInstanceExtensions` ([L6360](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L6360)) | `SDL_Vulkan_GetInstanceExtensions` ([SDL_vulkan.h L200](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_vulkan.h#L200)) |
| Surface | `glfwCreateWindowSurface(instance, window, alloc, &surface)`, callable from any thread ([L6455–L6510](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L6455-L6510)) | `SDL_Vulkan_CreateSurface(window, instance, alloc, &surface)` ([SDL_vulkan.h L226](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_vulkan.h#L226)) |
| macOS | GLFW creates and sets a `CAMetalLayer` on the content view for MoltenVK and prefers `VK_EXT_metal_surface` ([L6486–L6491](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L6486-L6491); [GL/src/cocoa_window.m L1929, L1964](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/src/cocoa_window.m#L1964)) | handled inside SDL |

Both signatures mention `VkInstance`/`VkSurfaceKHR`, so the call must sit in FenrirWindow's private sources or a backend-private file, per ADR-0010. `SDL_vulkan.h` declares the Vulkan handle types itself when `vulkan.h` was not included ([SDL_vulkan.h L59–L68](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_vulkan.h#L59-L68)). `glfw3.h` declares its Vulkan functions only when `VK_VERSION_1_0` is already defined, i.e. after `vulkan.h` or with `GLFW_INCLUDE_VULKAN` ([L110, L6362](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L6362)).

### Win32 `HWND` (D3D)

- **GLFW:** `glfwGetWin32Window(window)` in `glfw3native.h`, enabled by `#define GLFW_EXPOSE_NATIVE_WIN32` before the include, which pulls in `<windows.h>` ([GL/include/GLFW/glfw3native.h L225](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3native.h#L225)).
- **SDL3:** no separate native header. `SDL_GetWindowProperties(window)` returns a property bag; the `HWND` is under `SDL_PROP_WINDOW_WIN32_HWND_POINTER` (`"SDL.window.win32.hwnd"`) ([SDL_video.h L1628, L1650](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_video.h#L1628-L1650)). No `<windows.h>` is needed to fetch it as a `void*`.
- Either way, a D3D backend gets a `void*`/`HWND` and builds its own swapchain; neither library touches DXGI. A window for D3D would be created with no API (above).

### `CAMetalLayer` (Metal)

- **GLFW:** no Metal helper. 3.4 added `glfwGetCocoaView` (returns the `NSView`) next to `glfwGetCocoaWindow` ([GL/include/GLFW/glfw3native.h L288, L305](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3native.h#L288-L305); [GL/docs/news.md L121](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/news.md#L121)). The backend must create a `CAMetalLayer`, set it on the view and make the view layer-backed itself, in Objective-C(++).
- **SDL3:** `SDL_Metal_CreateView(window)` creates a `CAMetalLayer`-backed `NSView`/`UIView`, and `SDL_Metal_GetLayer(view)` returns the layer as `void*` ([SDL/include/SDL3/SDL_metal.h L56–L75, L103](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_metal.h#L56-L103)). It does not assign an `MTLDevice`; the caller does ([L59–L60](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_metal.h#L59-L60)). Main thread only.

macOS is out of scope for the map until a Metal backend exists; the difference is how much Objective-C that backend would carry.

---

## Input

### Delivery model and thread

- **GLFW: callbacks plus polled state.** Per-window callbacks (key, char, cursor position, button, scroll, drop, focus, size…) and global ones (joystick, monitor). `glfwGetKey`, `glfwGetCursorPos`, `glfwGetGamepadState` poll the last state ([L4901, L4968, L5897](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4901-L5897)). There is no event queue the application can read; FenrirWindow would build its own from callbacks. All callbacks except the error callback run on the main thread, during `glfwPollEvents`/`glfwWaitEvents*` ([GL/docs/intro.md L466–L468](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/intro.md#L466-L468)), but callbacks may also fire from inside other GLFW calls ([GL/include/GLFW/glfw3.h L4511–L4516](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4511-L4516)).
- **SDL3: an event queue plus polled state.** `SDL_PollEvent`/`SDL_WaitEvent(Timeout)` read a queue filled by `SDL_PumpEvents` ([SDL/include/SDL3/SDL_events.h L1085–L1106, L1310, L1334, L1364](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L1085-L1106)); both are main-thread only. `SDL_PushEvent` is safe from any thread ([L1385–L1398](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L1385-L1398)). Callbacks exist too: `SDL_AddEventWatch` and `SDL_SetEventFilter` see each event as it is queued, and the docs warn that the filter "may run in a different thread", except for `SDL_EVENT_WINDOW_EXPOSED`, which is guaranteed on the main thread ([L1425–L1440, L1463, L1514](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L1425-L1514)). Polled state: `SDL_GetKeyboardState`, `SDL_GetMouseState` ([SDL_keyboard.h L159](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_keyboard.h#L159); [SDL_mouse.h L310](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_mouse.h#L310)).

### Keyboard

- **GLFW** gives each key event a `GLFW_KEY_*` key (named after the US layout position) and a platform scancode. `glfwGetKeyName(key, scancode)` gives the layout-specific printable name, and `glfwGetKeyScancode(key)` maps back ([L4835, L4862](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4835-L4862); [GL/docs/input.md L104–L200, L233](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/input.md#L104-L200)).
- **SDL3** gives each `SDL_KeyboardEvent` both an `SDL_Scancode` (physical, USB HID based) and an `SDL_Keycode` (layout-mapped virtual key) ([SDL_events.h L362–L368](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L362-L368)), with `SDL_GetKeyFromScancode(scancode, modstate, key_event)`, `SDL_GetScancodeFromKey` and `SDL_GetKeyName` for conversion ([SDL_keyboard.h L231, L252, L333](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_keyboard.h#L231-L333)).

For game bindings both offer a layout-independent physical code; for UI shortcuts both offer the layout-mapped key.

### Text input and IME

- **GLFW 3.4:** a character callback receives UTF-32 code points after layout, modifiers and dead keys ([GL/docs/input.md L204–L231](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/input.md#L204-L231); `glfwSetCharCallback` [L5241](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L5241)). There is no IME API: no composition (pre-edit) text, no candidate list, no way to place the IME window. That is open as issue [#41 "IME input"](https://github.com/glfw/glfw/issues/41) with pull request [#2130 "Add IME support for each platform"](https://github.com/glfw/glfw/pull/2130), both still open, and not in 3.5.1.
- **SDL3:** text events are off until `SDL_StartTextInput(window)` ([SDL_keyboard.h L353–L378](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_keyboard.h#L353-L378)); then `SDL_EVENT_TEXT_INPUT` (UTF-8), `SDL_EVENT_TEXT_EDITING` (composition) and `SDL_EVENT_TEXT_EDITING_CANDIDATES` arrive ([SDL_events.h L173–L178](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L173-L178)). `SDL_SetTextInputArea` tells the OS where to place the IME window ([L550](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_keyboard.h#L550)). `SDL_HINT_IME_IMPLEMENTED_UI` picks whether the OS or the app draws the composition and candidates ([SDL/include/SDL3/SDL_hints.h L1244–L1256](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_hints.h#L1244-L1256)). This matters for the user's own GUI library later (text fields).

### Mouse

Both give position, buttons, scroll and enter/leave. GLFW positions are `double` in screen coordinates; SDL3's are `float` ([SDL_events.h L455–L463](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L455-L463)). GLFW 3.5 removed the cap of eight mouse buttons ([3.5.1 docs/news.md L16](https://github.com/glfw/glfw/blob/d9d6f0f1f967807ffade6598ea9a631ebaf37a56/docs/news.md#L16)).

### Gamepads

| | GLFW 3.4 | SDL3 |
|---|---|---|
| Mapping database | SDL_GameControllerDB format; a copy built in at release; `glfwUpdateGamepadMappings(string)` adds more at runtime; a CMake target `update_mappings` regenerates `src/mappings.h` ([GL/docs/input.md L731–L760, L814–L832](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/input.md#L731-L832); [L5827](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L5827)) | The same database (SDL owns the format); built in as `src/joystick/SDL_gamepad_db.h`; `SDL_AddGamepadMapping`, `SDL_AddGamepadMappingsFromFile` ([SDL/include/SDL3/SDL_gamepad.h L347, L421](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_gamepad.h#L347-L421)). Also drives many controllers directly over HIDAPI (`SDL_HIDAPI_JOYSTICK`, [SDL/CMakeLists.txt L381–L385](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/CMakeLists.txt#L381-L385)) |
| Hot plug | Joystick callback with `GLFW_CONNECTED`/`GLFW_DISCONNECTED`; needs event processing to be delivered on all platforms ([GL/docs/input.md L694–L722](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/input.md#L694-L722); [L5793](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L5793)) | `SDL_EVENT_GAMEPAD_ADDED`/`REMOVED`/`REMAPPED` events ([SDL_events.h L205, L673](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L205)) |
| State | Polled: `glfwGetGamepadState` fills buttons and axes ([L5897](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L5897)) | Events, or polled with `SDL_GetGamepadAxis` etc. ([SDL_gamepad.h L1275](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_gamepad.h#L1275)) |
| Rumble | **None.** Open issue [#57 "Support for haptic feedback (vibration, rumble)"](https://github.com/glfw/glfw/issues/57) | `SDL_RumbleGamepad`, `SDL_RumbleGamepadTriggers`, plus `SDL_SetGamepadLED`, touchpads and sensors ([SDL_gamepad.h L1430, L1536, L1566, L1588](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_gamepad.h#L1430-L1588)) |

### Touch and pen

- **GLFW:** no touch or pen API; touchpad gestures arrive only as scroll ([GL/docs/input.md L546–L549](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/input.md#L546-L549)). Touch is open issue [#42 "Touch input"](https://github.com/glfw/glfw/issues/42).
- **SDL3:** `SDL_EVENT_FINGER_*` touch events and `SDL_EVENT_PEN_*` pen events with pressure ([SDL_events.h L216, L249, L783, L819–L888](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L216-L249); [SDL/docs/README-touch.md](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/docs/README-touch.md)).

---

## The loop

### Event pumping

- **GLFW:** `glfwPollEvents` (return at once), `glfwWaitEvents`, `glfwWaitEventsTimeout(seconds)`; `glfwPostEmptyEvent` from any thread wakes a waiting main thread ([L4534, L4628, L4648](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4534-L4648); [GL/docs/input.md L30–L60](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/input.md#L30-L60)). Event processing must run even with no window, to get joystick and monitor connection events.
- **SDL3:** `SDL_PollEvent`, `SDL_WaitEvent`, `SDL_WaitEventTimeout(ms)`, or `SDL_PumpEvents` plus polled state. Optionally the **main-callbacks** model (`SDL_MAIN_USE_CALLBACKS`: `SDL_AppInit`/`SDL_AppIterate`/`SDL_AppEvent`/`SDL_AppQuit`), which hands the loop to SDL; it is optional, and so is `SDL_main.h` on Windows ([SDL/docs/README-main-functions.md L60–L80](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/docs/README-main-functions.md#L60-L80)). FenrirApp owns the loop today, so the plain `SDL_PollEvent` model is the one that fits without inverting control.

### What blocks: moving and resizing on Windows

Win32 runs its own modal loop while the user drags or resizes a window, so the application's loop does not run until the drag ends.

- **GLFW 3.4:** documented: "a window move, resize or menu operation will cause event processing to block" and the advice is to redraw from the window refresh callback ([GL/include/GLFW/glfw3.h L4504–L4509](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4504-L4509); `glfwSetWindowRefreshCallback` [L4340](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L4340)). In the source, `WM_ENTERSIZEMOVE` only releases a disabled or captured cursor; no timer runs, and refresh comes from `WM_PAINT` damage ([GL/src/win32_window.c L991–L1005, L1142–L1146](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/src/win32_window.c#L991-L1146)). During a drag the app gets size and refresh callbacks from inside `glfwPollEvents`, and must draw from inside them.
- **SDL3:** `SDL_PollEvent`'s doc warns it can block for "significant amounts of time" during drag/resize and points to the wiki page AppFreezeDuringDrag ([SDL_events.h L1290–L1296](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L1290-L1296)). In the source, `WM_ENTERSIZEMOVE` starts a `USER_TIMER_MINIMUM` timer and resets the keyboard ([SDL/src/video/windows/SDL_windowsevents.c L1897–L1924](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/src/video/windows/SDL_windowsevents.c#L1897-L1924)). Each `WM_TIMER` calls `SDL_OnWindowLiveResizeUpdate`, which runs one `SDL_AppIterate` if main callbacks are in use, and otherwise sends `SDL_EVENT_WINDOW_EXPOSED` ([L1926–L1950](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/src/video/windows/SDL_windowsevents.c#L1926-L1950); [SDL/src/video/SDL_video.c L4329–L4339](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/src/video/SDL_video.c#L4329-L4339)). An app with its own loop can catch that event in an event watch, on the main thread, and render a frame from there ([SDL_events.h L139, L1435–L1438](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_events.h#L139)).

Either way, rendering during a drag means re-entering the frame from inside the event pump. SDL3 supplies a steady tick for it; GLFW delivers only the OS's paint and size messages.

### Threading rules

- **GLFW:** most functions main thread only; the reference states it per function (93 functions in `glfw3.h` are marked main-thread only, 30 any-thread). Init, termination, event processing, and creating or destroying windows, cursors and contexts are main thread only. Any thread may call `glfwPostEmptyEvent`, the error query, the window user pointer and close flag (unsynchronised), the context functions listed above, `glfwCreateWindowSurface`, and the raw timer ([GL/docs/intro.md L454–L500](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/intro.md#L454-L500)).
- **SDL3:** the video, mouse and keyboard headers mark nearly everything "should only be called on the main thread" (115 functions in `SDL_video.h`, GL functions included). `SDL_IsMainThread` and `SDL_RunOnMainThread(callback, userdata, wait_complete)` let other threads hand work to the main thread; the latter is safe from any thread ([SDL/include/SDL3/SDL_init.h L330, L355–L367](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_init.h#L330-L367)). Event watchers may run on other threads (above).
- **For the renderer:** if the frame is ever rendered on a thread other than the one pumping events, GLFW documents GL make-current and swap as allowed off the main thread and SDL3 does not. Vulkan, D3D and Metal submission does not go through either library once the surface or layer exists.

---

## Beyond windowing (SDL3 only)

GLFW is windowing, contexts/surfaces, input, monitors, clipboard (`glfwSetClipboardString`/`glfwGetClipboardString`, [L5927, L5962](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/include/GLFW/glfw3.h#L5927-L5962)), path drop and a timer. Nothing else.

SDL3's CMake defines each subsystem as an option, on by default: Audio, Video, GPU, Render, Camera, Joystick, Haptic, Hidapi, Power, Sensor, Dialog ([SDL/CMakeLists.txt L236–L262](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/CMakeLists.txt#L236-L262)). GPU, Render and Camera depend on Video.

- **GPU API** (`SDL_gpu.h`): a Vulkan/D3D12/Metal-style 3D and compute API ([SDL/include/SDL3/SDL_gpu.h L24–L36](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_gpu.h#L24-L36)). It has three backends, Vulkan, D3D12 and Metal, and **no OpenGL backend**. Shaders must be supplied per backend (`SDL_GPU_SHADERFORMAT_SPIRV`, `_DXIL`, `_MSL`, [L1037–L1040](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_gpu.h#L1037-L1040)), with the separate project SDL_shadercross for cross-compiling ([L120–L135](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_gpu.h#L120-L135)). The device claims a window with `SDL_ClaimWindowForGPUDevice` ([L4151](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_gpu.h#L4151)). It would be an alternative to writing the Vulkan/D3D/Metal backends behind the graphics seam, not an addition to an OpenGL backend; using it would make SDL the graphics backend, which the seam ticket has to weigh.
- **Audio** (`SDL_audio.h`, e.g. `SDL_OpenAudioDeviceStream`, [L2058](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_audio.h#L2058)). Audio is out of scope for this map.
- **File dialogs** (`SDL_ShowOpenFileDialog`, `SDL_ShowSaveFileDialog`, asynchronous with a callback, [SDL/include/SDL3/SDL_dialog.h L166, L215](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_dialog.h#L166-L215)). Useful to an editor; GLFW has none.
- **Clipboard** (`SDL_SetClipboardText`, [SDL/include/SDL3/SDL_clipboard.h L104](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/include/SDL3/SDL_clipboard.h#L104)); both libraries have it.
- **Cost of leaving them out:** each is a CMake option; turning off Audio, GPU, Render, Camera, Haptic, Power, Sensor and Dialog leaves Video, Joystick, Hidapi (gamepads) and the core. The measured effect on build time and library size is in the next section. SDL code that is compiled in but never called costs only disk and link time, since the library is static.

---

## Build

### CMake as a submodule

- **GLFW:** `add_subdirectory`; the repo already does this for the DemoApp with `GLFW_BUILD_DOCS/TESTS/EXAMPLES OFF` ([examples/DemoApp/libs/CMakeLists.txt](../../../examples/DemoApp/libs/CMakeLists.txt)). Options: `BUILD_SHARED_LIBS` (default OFF), `GLFW_BUILD_EXAMPLES`/`GLFW_BUILD_TESTS` (default on only when GLFW is the top-level project), `GLFW_BUILD_DOCS` (default ON), `GLFW_INSTALL`, per-platform `GLFW_BUILD_WIN32/COCOA/X11/WAYLAND`, and `GLFW_LIBRARY_TYPE` to override static/shared for GLFW alone ([GL/CMakeLists.txt L17–L55](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/CMakeLists.txt#L17-L55)). One target, `glfw`. Since 3.4, tests and examples are off as a subproject ([GL/docs/news.md L191](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/docs/news.md#L191)).
- **SDL3:** `add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)` is the documented vendored path ([SDL/docs/README-cmake.md L71–L118](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/docs/README-cmake.md#L71-L118)). Targets `SDL3::SDL3-static` / `SDL3::SDL3-shared`, with `SDL3::SDL3` aliasing whichever exists ([L108–L114](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/docs/README-cmake.md#L108-L114)). With neither `SDL_SHARED`/`SDL_STATIC` nor `BUILD_SHARED_LIBS` set, SDL builds **shared only**; set `SDL_STATIC=ON SDL_SHARED=OFF`, or define `BUILD_SHARED_LIBS=OFF` ([SDL/CMakeLists.txt L194–L232](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/CMakeLists.txt#L194-L232); [README-cmake.md L137–L144](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/docs/README-cmake.md#L137-L144)). Turn off `SDL_TEST_LIBRARY`, `SDL_INSTALL`, and the subsystems not wanted ([CMakeLists.txt L287–L411](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/CMakeLists.txt#L287-L411)).

### Static linking and the dynamic API

SDL builds a jump table of function pointers into every build, static included. Setting the `SDL_DYNAMIC_API` environment variable to another SDL3 library makes a statically linked game use that library instead ([SDL/docs/README-dynapi.md](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/docs/README-dynapi.md)). That is SDL's answer to "static linking stops users replacing SDL". GLFW has no such mechanism.

### Licence

- GLFW: zlib/libpng licence ([GL/LICENSE.md](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/LICENSE.md)).
- SDL3: zlib licence ([SDL/LICENSE.txt](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/LICENSE.txt)). The bundled HIDAPI is under the user's choice of GPLv3, BSD-style or its original licence ([SDL/src/hidapi/LICENSE.txt](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/src/hidapi/LICENSE.txt)).

Neither requires an attribution notice in binaries.

### Size

| | GLFW 3.4 | SDL 3.4.18 |
|---|---|---|
| Source tree without `.git` | 5.3 MB | 54 MB |
| Lines in `src/**/*.{c,h,m}` | 35,789 | 574,500 (all platforms) |
| Static `.lib`, clang Release | 0.56 MB | 9.8 MB |
| Static `.lib`, MSVC Release | 0.72 MB | 13.8 MB |
| SDL with Audio, GPU, Render, Camera, Haptic, Power, Sensor, Dialog off | — | clang 8.1 MB, MSVC 11.5 MB |
| Translation units (Windows) | 21 | 272 (default), fewer with subsystems off |
| Clean build time, clang / MSVC (this machine, Ninja, parallel) | 2 s / 1 s | 36 s / 6 s (minimal: 18 s / 5 s) |

`.lib` size is not binary size: the linker keeps only what is referenced. The clang build times are inflated by printing the warnings below.

### Warnings under FenrirEngine's flags

FenrirEngine sets warnings with directory-scoped `add_compile_options` in the root `CMakeLists.txt`: clang gets `-Weverything` minus nine `-Wno-*`, MSVC gets `/W4` ([CMakeLists.txt L10–L23](../../../CMakeLists.txt#L10-L23)). These options are not language-filtered, so **they also reach any C library added with `add_subdirectory` under that directory**. The probe built each library as a subdirectory under exactly those flags.

**Compiling the libraries themselves** (warnings, not errors; neither uses `-Werror`/`/WX` by default):

| | GLFW 3.4 | SDL 3.4.18 |
|---|---|---|
| clang `-Weverything` set: warnings (unique) | 8,354 (1,439) | 35,713 (24,516); minimal: 27,307 (18,785) |
| Top clang categories | `-Wreserved-identifier` 6,151, `-Wunsafe-buffer-usage` 574, `-Wdocumentation` 420 | `-Wunsafe-buffer-usage` 19,036, `-Wimplicit-int-conversion` 2,579, `-Wreserved-macro-identifier` 2,013 |
| MSVC `/W4` | 0 code warnings; 21× `D9025: overriding '/W4' with '/W3'` | 0 code warnings; 272× `D9025` |

Both libraries add their own warning level on their targets, which wins over the root's: GLFW adds `/W3` (MSVC) or `-Wall` ([GL/src/CMakeLists.txt L248–L257](https://github.com/glfw/glfw/blob/7b6aead9fb88b3623e3b3725ebb42670cbe4c579/src/CMakeLists.txt#L248-L257)); SDL adds `/W3` for MSVC ([SDL/cmake/sdlcompilers.cmake L38–L40, L59–L62](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/cmake/sdlcompilers.cmake#L38-L62)), `-Wall -Wundef -Wfloat-conversion -Wdocumentation…` for clang, and rewrites `/W4` to `/W3` under clang-cl ([SDL/CMakeLists.txt L453–L461](https://github.com/libsdl-org/SDL/blob/829a65d769d935c4852f8159e964312c0957260a/CMakeLists.txt#L453-L461)). So MSVC is quiet apart from one D9025 line per file, while clang's `-Weverything` is additive and floods the build log. Neither build fails. Adding the library from a directory the root options do not reach, or removing them from its targets, silences both.

**Including the public headers** in a C++20 file (what FenrirWindow's private `.cpp` files would do):

| | GLFW (`glfw3.h` + `glfw3native.h` with `GLFW_EXPOSE_NATIVE_WIN32`) | SDL3 (`SDL.h` + `SDL_vulkan.h`) |
|---|---|---|
| clang `-Weverything` set, `-I` | 34 warnings: `-Wdocumentation` (20), `-Wdocumentation-unknown-command` (11), `-Wdocumentation-deprecated-sync` (2), and `-Wnonportable-system-include-path` from `#include <windows.h>` vs `Windows.h` on disk (1) | 4 warnings: `-Wreserved-macro-identifier` (2), `-Wold-style-cast` (1, `SDL_bits.h`), `-Wdocumentation-unknown-command` (1) |
| clang, `-isystem` (CMake `SYSTEM`) | 0 | 0 |
| MSVC `/W4` | 0 | 0 |

Including either library as `SYSTEM` (as the DemoApp already does for GLFW) silences its headers.

---

## Summary for FenrirWindow

- **Feature parity on the basics:** both create several windows, exclusive and borderless fullscreen, resize, minimise, report a content scale and a pixel size, hide, confine and capture the cursor with relative motion, give scancodes and layout keys, make OpenGL contexts with swap interval, make Vulkan surfaces and expose an `HWND`.
- **Where SDL3 offers more:** IME composition and candidates, gamepad rumble, LEDs, touchpads and sensors, touch and pen, a `CAMetalLayer` helper, file dialogs, audio, a GPU API (Vulkan/D3D12/Metal, no GL), a steady tick during Windows drag/resize, an event queue.
- **Where GLFW is lighter or looser:** a tenth of SDL's source tree and a sixteenth of its lines; a 0.6–0.7 MB static library against SDL's 8–14 MB; one target; GL make-current and swap allowed from any thread; Vulkan surface creation from any thread; callbacks instead of a queue.
- **Shared constraints:** the graphics API is fixed at window creation in both; rendering during a Windows drag means rendering from inside the event pump in both; event pumping is main-thread only in both.
- **Open upstream for GLFW:** IME ([#41](https://github.com/glfw/glfw/issues/41), [PR #2130](https://github.com/glfw/glfw/pull/2130)), rumble ([#57](https://github.com/glfw/glfw/issues/57)), touch ([#42](https://github.com/glfw/glfw/issues/42)).
