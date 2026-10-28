---
status: accepted
---

# FenrirInput owns input and FenrirWindow feeds it

Keyboard, mouse and gamepad input reaches the engine through FenrirWindow's windowing library (GLFW or SDL3, ticket 09). Both libraries deliver gamepads too, and both ship SDL_GameControllerDB (branch `research/glfw-or-sdl3`). Game code, C# scripts and tests read input, but they should not depend on windowing to do it. So input has its own package, **FenrirInput**, which links no library. FenrirWindow depends on it and feeds it.

## The split

- **FenrirInput holds the types, the state and the events.** It defines the device-neutral types: `Key` (a key code, which follows the keyboard layout), `Scancode` (a physical key position), modifiers, `MouseButton`, gamepad buttons and axes. It also defines the polled resources (`Keyboard`, `Mouse`, `Gamepads`) and the input events. Nothing in it names GLFW, SDL or the OS.
- **Input comes in through a public input feed.** FenrirWindow pumps its library at the start of `PreUpdate` and passes each input it gets to the feed, translated into FenrirInput's types. FenrirInput then updates the resources and sends the events, still in `PreUpdate`, so every later phase of the frame sees that frame's input. Input routers (ADR-0027) see each input first and may consume it before it reaches the resources and events.
- **FenrirInput installs alone.** `FenrirInput::Install(App&)` works without a window. A test or a dedicated server then gets only the input that code feeds in through the same feed, so tests can script key presses. `FenrirWindow::Install` installs FenrirInput if it isn't installed already.
- **Cursor modes stay on FenrirWindow.** Normal, hidden, and captured with relative motion are window properties. FenrirInput only reports the motion.

## How game code reads it

- **Polled state and events, both.** `Keyboard`, `Mouse` and `Gamepads` are app-wide resources. They give what is held now, plus "just pressed" and "just released". Events (keys, text, mouse buttons, motion, scroll, gamepad connection and buttons) go through `EventReader<T>`, so each reader sees every press in order, once (ADR-0012).
- **Edges follow the phase that reads them.** A call made from `PreTick`, `Tick` or `PostTick` answers with the edges gathered since the last tick. A call made from any other phase answers with the edges since the last frame. Tick edges are cleared after each tick's `PostTick`, so a tap is never lost between ticks or seen in two ticks. A tap that starts and ends between two ticks reads as just pressed and just released in the next tick, with held false.
- **One input state, events name their window.** Keyboard and mouse events say which window they came from, so several windows stay possible (ticket 11) without changing the shape. Mouse position is in the focused window's logical coordinates, as floats, from the top-left. Motion is in the same units, raw while the cursor is captured. Scroll is in notches. The renderer converts to framebuffer pixels with the content scale.

## The first version

Keys by key code and by scancode, with modifiers and repeat. Text input as Unicode codepoints. Mouse buttons, position, motion and scroll. Cursor modes. Gamepads in the standard layout: buttons, axes, triggers, several pads, hot plug as connect and disconnect events. Rumble, IME composition, touch and pen are left out. Only SDL3 offers them, so the first version does not tie ticket 09's choice.

C# gets the same as C++ through FenrirInput's scripting extension (ADR-0009): the read-only resources with the same phase-following edges, and the events through `EventReader` fields.

## Considered options

- **Input inside FenrirWindow.** One package fewer, but anything that only reads input, C# included, would depend on windowing, and tests would need a window backend to inject a key press.
- **FenrirInput reads devices itself.** It would need a second library, or raw OS code, for gamepads, and a pump of its own beside the window's.
- **Edges per frame only.** Simpler, but a `Tick` system polling "just pressed" misses a tap in a frame with no tick and sees it twice in a frame with two.
- **Choosing the edge set at the call** (`SinceFrame()`, `SinceTick()`). Nothing hidden, but a `Tick` system that picks the frame set gets the same bug.
- **Events only, or polled state only.** With events only, every system keeps its own set of held keys. With polled state only, text input and key order are lost.

## Consequences

- The DemoApp's `Events.hpp` input structs and its GLFW callbacks were not migrated; they went with the DemoApp (the renderer foundation, PR #88).
- Input mapping (actions and bindings) builds on FenrirInput later, out of this map's scope.
- If an SDL3-only feature (rumble, IME, touch) is wanted, it joins FenrirInput's types and the feed, and only the window's library adapter changes.
