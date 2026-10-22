# 23: The Fenrir.Input and Fenrir.Window scripting extensions

**What to build:** C# reads input with the same phase-following edges and events as C++, and manages the window (spec: "Scripting: the extensions" (one extension per package, `Fenrir.Input`, `Fenrir.Window`, `InputClaims`, no-op rules), "Testing Decisions" seam 7 (fed keys); ADR-0029, ADR-0017).

- **Layout per package:** contracts in `packages/FenrirInput/Managed/Fenrir.Input/` and `packages/FenrirWindow/Managed/Fenrir.Window/`, native functions in static `InputScripting` and `WindowScripting` libraries, so neither package links FenrirScripting. Each builds only with `FENRIR_ENABLE_SCRIPTING` and its package's option (Input always; Window with `FENRIR_ENABLE_RENDERER`). The app adds `Input::ScriptingExtension()` and `Window::ScriptingExtension()`; each is static and needs no installed package.
- **`Fenrir.Input`:** mirrored enums `Key`, `Scancode`, `KeyModifiers`, `MouseButton`, `GamepadButton`, `GamepadAxis`; facades `Keyboard` (`IsDown`, `JustPressed`, `JustReleased` by key or scancode, `Modifiers`), `Mouse` (buttons, `Position`, `Motion`, `Scroll`) and `Gamepads` (`IsConnected`, buttons, `Axis` per pad), with C++'s phase-following edges; every input event as a native event read through `EventReader<T>`; `InputClaims` as a read-only app-resource mirror. Routers stay C++.
- **`Fenrir.Window`:** `WindowId` as a plain struct; `Window.Main`; `Size`, `PixelSize`, `ContentScale`, `IsMinimised`, `IsFocused` by `WindowId`; `CursorMode` get and set, `SetCursorShape`; `Clipboard` get and set; `SetTitle`; `RequestClose`; resized, focus and close-requested as native events. Text getters fill a caller's `Span<char>` and return the length.
- **No-op rules:** with the package absent, no window or a stale id, functions do nothing and return `false`, zero or an invalid value; a debug build warns once per native call site; nothing throws.

**Blocked by:** 04 (Input routers, control owners and input claims); 21 (FenrirScripting: native resources and the change buffer on the call context); 22 (The Fenrir.Math shared assembly).

**Touches:** `packages/FenrirInput/Managed/`, `packages/FenrirInput/Scripting/`, `packages/FenrirWindow/Managed/`, `packages/FenrirWindow/Scripting/`, `CMakeLists.txt`, `FenrirScripting.sln`

**Status:** ready-for-agent

- [ ] Extension tests (scripting harness, input feed): a key fed before a frame reads as `Keyboard.JustPressed` in a C# `Update` system that frame and in a C# tick system at the next tick, matching C++ per phase; the key event arrives once in a C# `EventReader`.
- [ ] Mouse, gamepad and `InputClaims` read from C# match C++.
- [ ] Each enum mirror and `WindowId` match C++'s values and layout.
- [ ] With no window installed, every `Fenrir.Window` call no-ops with the documented returns and one debug warning per call site; a hidden-window test reads `Size` and round-trips the clipboard (skipped without a display).
- [ ] Builds with scripting off (no extension targets) and with the renderer off (no `Fenrir.Window`).
