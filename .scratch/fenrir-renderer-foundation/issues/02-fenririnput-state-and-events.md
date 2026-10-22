# 02: FenrirInput: the input feed, Keyboard, Mouse and Gamepads, and input events

**What to build:** FenrirInput exists as an always-built package with no library, tested headlessly through its input feed (spec: "FenrirInput" (package, types, input feed, app resources, units, events), "Testing Decisions" seam 3; ADR-0017, ADR-0012). Routers and claims are ticket 04.

- **Package:** replaces the empty `packages/FenrirInput` placeholder; always built like FenrirTime, no library linked. `Input::Install(App&)` returns its owner (it cannot fail) and works with no window.
- **Types:** `Key` (key code, follows the layout), `Scancode` (physical position), `KeyModifiers`, `MouseButton`, `GamepadButton` and `GamepadAxis` in the standard layout. A plain `WindowId` the events name (FenrirWindow reuses it).
- **The input feed:** a public API through which FenrirWindow, or a test, passes neutral input in: key down and up (key, scancode, modifiers, repeat, window), text (codepoints, window), mouse button, move (logical position and motion), scroll (notches), focus gained and lost (window), and gamepad connect, disconnect, button and axis. Everything fed is processed in `PreUpdate`, by a sequential system.
- **App resources** `Keyboard`, `Mouse` and `Gamepads`: held, just pressed and just released. **Edges follow the reading phase:** from `PreTick`, `Tick` or `PostTick` they are the edges since the last tick, cleared after each `PostTick`; from any other phase, since the last frame. A tap that starts and ends between two ticks reads as just pressed and just released in the next tick, with held false. Repeats are not new presses.
- **Units:** one app-wide state; mouse position in the focused window's logical coordinates (float, top-left), motion in the same units (raw while captured), scroll in notches.
- **Events:** every input is also an engine event read through `EventReader<T>`, once per reader, in order.
- README for the package, including the main-thread rule for its dispatch system.

**Blocked by:** 01 (Engine prerequisites).

**Touches:** `packages/FenrirInput/`, `CMakeLists.txt`

**Status:** ready-for-agent

- [ ] `FenrirInputTests` (Catch2, through `App::RunFrame` and the feed): held, just pressed and just released read from `Update` and from `Tick`; a frame with zero ticks keeps the tick edges for the next tick; a frame with two ticks shows the edge only in the first; a tap between ticks reads as pressed and released with held false; repeats do not re-press.
- [ ] Key code and scancode are tracked separately; modifiers are reported; text events carry codepoints and the window.
- [ ] Mouse position, motion and scroll are in the documented units; gamepads connect and disconnect (hot plug) and read buttons and axes per pad.
- [ ] Every fed input arrives once per `EventReader<T>`, in order, and a second reader sees it too.
- [ ] Clang and MSVC x64 debug and release presets build with no new warnings.
