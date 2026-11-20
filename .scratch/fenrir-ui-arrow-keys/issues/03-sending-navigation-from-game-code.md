# 03: Sending navigation from game code

Spec: [Arrow keys move UI focus](../spec.md).

**What to build:** A game sends a root a navigation command, a direction (up, down, left, right), activate or cancel, from C++ and from C#, so it can map W and S or any action to menu movement. A direction goes through the same navigation as an arrow key, and with nothing focused gives the root its first focus as a gamepad direction does; activate sends the focused element what the gamepad's A sends; cancel sends Cancel.

**Blocked by:** 02
**Touches:** `packages/FenrirUI/include/FenrirUI/`, `packages/FenrirUI/src/`, `packages/FenrirUI/Tests/`, `packages/FenrirUI/api/`, `packages/FenrirUI/Scripting/`, `packages/FenrirUI/Managed/`

**Status:** ready-for-agent

Stacked on 02 (top of the stack).

- [ ] FenrirUI tests: a sent direction moves focus as an arrow does; activate presses the focused button; cancel sends Cancel; a direction with nothing focused gives the first focus.
- [ ] FenrirUIScripting fixture tests: a direction, activate and cancel sent from C# reach the root.
- [ ] The API description and generated bindings are current.
