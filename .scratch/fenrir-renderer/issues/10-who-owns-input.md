# Who owns input: FenrirWindow or a FenrirInput package?

Type: grilling
Status: resolved
Blocked by: 03

## Question

Who owns keyboard, mouse, gamepad and text input? **ADR.**

- Input inside FenrirWindow (the windowing library delivers it), or a separate FenrirInput package (the placeholder exists) that FenrirWindow feeds, or that reads devices itself?
- How does game code read input: polled state, engine events (`EventReader<T>`, ADR-0012), or both? In which FenrirApp phase is it gathered, and how do fixed-step systems (`Tick`) see presses that happened between ticks?
- What does the first version cover: keys (by key code and by scancode), text input, mouse buttons, motion and scroll, cursor modes, gamepads?
- What does C# get?

Input mapping (actions and bindings) is out of scope.

## Answer

ADR: `docs/adr/0017-fenririnput-owns-input-and-fenrirwindow-feeds-it.md`. Glossary: Key code, Scancode, Input feed, Input edge.

- **Owner:** FenrirInput, which links no library. It owns the device-neutral types (`Key`, `Scancode`, modifiers, `MouseButton`, gamepad buttons and axes), the app-wide polled resources (`Keyboard`, `Mouse`, `Gamepads`) and the input events. FenrirWindow depends on it and, after pumping its library at the start of `PreUpdate`, passes input in through FenrirInput's public input feed. FenrirInput updates the state and sends the events, still in `PreUpdate`.
- **Headless:** `FenrirInput::Install(App&)` works without a window, and tests feed input through the same feed. `FenrirWindow::Install` installs FenrirInput if it isn't installed already. Cursor modes (normal, hidden, captured) are FenrirWindow's.
- **Reading:** polled state (held, just pressed, just released) and events through `EventReader<T>` (ADR-0012), both. Edges follow the reading phase: from `PreTick`/`Tick`/`PostTick` they are the edges since the last tick (cleared after each `PostTick`), and from any other phase the edges since the last frame. A tap between two ticks reads as just pressed and just released in the next tick.
- **Windows and units:** one app-wide state; keyboard and mouse events name their window. Mouse position is in the focused window's logical coordinates (float, top-left), motion in the same units (raw while captured), scroll in notches.
- **First version:** keys by key code and scancode with modifiers and repeat, text as codepoints, mouse buttons, position, motion and scroll, cursor modes, standard-layout gamepads with hot plug. Rumble, IME composition, touch and pen are left out (SDL3 only, so ticket 09 stays open).
- **C#:** the same through FenrirInput's scripting extension (ADR-0009): read-only resources with phase-following edges, and events through `EventReader` fields.
