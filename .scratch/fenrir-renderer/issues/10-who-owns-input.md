# Who owns input: FenrirWindow or a FenrirInput package?

Type: grilling
Status: claimed
Blocked by: 03

## Question

Who owns keyboard, mouse, gamepad and text input? **ADR.**

- Input inside FenrirWindow (the windowing library delivers it), or a separate FenrirInput package (the placeholder exists) that FenrirWindow feeds, or that reads devices itself?
- How does game code read input: polled state, engine events (`EventReader<T>`, ADR-0012), or both? In which FenrirApp phase is it gathered, and how do fixed-step systems (`Tick`) see presses that happened between ticks?
- What does the first version cover: keys (by key code and by scancode), text input, mouse buttons, motion and scroll, cursor modes, gamepads?
- What does C# get?

Input mapping (actions and bindings) is out of scope.
