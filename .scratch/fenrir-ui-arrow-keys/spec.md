# Arrow keys move UI focus

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [Do arrow keys move UI focus?](../engine-gaps/issues/30-do-arrow-keys-move-ui-focus.md). Settles [Arrow keys do not move focus in FenrirUI](../engine-gaps/issues/10-arrow-key-focus-in-ui.md).

## Problem Statement

FenrirUI moves focus with Tab and Shift+Tab in tree order, and with a gamepad's d-pad and left stick to the nearest element in a direction. The keyboard's arrow keys do nothing to focus; only controls that use them themselves (a slider, a scroll view, a text field) read them. Escape does not cancel the way a gamepad's B does. So a keyboard player cannot move through a title, pause or results menu the way a gamepad player does, and a game that wants W and S to move through its menus has no way to send a direction to the UI.

## Solution

While a UI root has the focus, the arrow keys send the same directions the d-pad sends into directional navigation: each is offered to the focused element first, so a control that uses arrows keeps them, and otherwise the focus moves to the nearest element that way. Escape sends Cancel as B does, where the focused element does not take it; Enter and Space already activate. A game sends a direction, activate or cancel to a root itself through a public call from C++ and C#, so it maps W and S or any action to menu movement. The root's `gamepadNavigation` flag becomes `directionalNavigation`, covering the arrow keys too, so a root that leaves directions to the game, like the demo UI kit's, leaves the arrows too.

## User Stories

1. As a keyboard player, I want the arrow keys to move between buttons in a menu, so that I can play without a mouse.
2. As a keyboard player, I want the focus to move to the nearest control in the direction I press, so that a grid of buttons moves as it looks.
3. As a keyboard player, I want holding an arrow to keep moving, so that a long list scrolls through.
4. As a keyboard player, I want Enter or Space to press the focused control, so that I can choose an option.
5. As a keyboard player, I want Escape to go back out of a menu, so that the keyboard does what a gamepad's B does.
6. As a keyboard player, I want a slider to take left and right, so that I can change its value.
7. As a keyboard player, I want a text field to keep its arrow keys for its caret, so that I can edit text.
8. As a keyboard player, I want a scroll view to keep its arrows when it has them, so that I can scroll its content.
9. As a game developer, I want the arrow keys to pass to my game when no element in the UI has the focus, so that arrow controls still work during play.
10. As a game developer, I want a root that leaves directions to the game to leave the arrow keys to it too, so that a debug panel never steals movement keys.
11. As a game developer, I want to send a direction to a UI root from my code, so that W and S move through my menus.
12. As a game developer, I want to send activate and cancel to a root from my code, so that any action of mine can press and go back.
13. As a game developer, I want a sent direction to behave as an arrow key does, offered to the focused element first, so that the same rules hold.
14. As a C# game developer, I want to send directions, activate and cancel from C#, so that my Razor menus are driven by my input code.
15. As an engine maintainer, I want one flag that covers the gamepad and the arrow keys, so that a root's choice is one setting.
16. As an engine maintainer, I want UIDemo's menus to work with the arrow keys, Enter and Escape, so that I can check it by hand.

## Implementation Decisions

- **Arrow keys** (Up, Down, Left, Right) pressed while a root's element has the focus are turned into the matching directions and go through the same navigation the d-pad's do: offered to the focused element alone, then a move to the nearest focusable element, revealed if scrolled away. A control that takes the arrow key (it consumed the key event, as the slider, scroll view and text field do) keeps it, and no move happens. Repeats come from the keyboard's own key repeat, not the gamepad's hold timer.
- **Escape**, when the focused element does not take it, sends the Cancel UI event as the gamepad's B does.
- **Enter, keypad Enter and Space** already activate the focused control; unchanged.
- **With no element of the root focused,** arrow keys and Escape pass to the game (the keyboard goes to the UI only while it has focus, ADR-0065); unlike a gamepad direction, an arrow does not give a root the first focus.
- **The flag:** `UIRootConfig::gamepadNavigation` is renamed `directionalNavigation` and covers the d-pad, the left stick and the arrow keys; off, the root takes none of them. The demo UI kit keeps it off, so demo scenes keep their arrow keys. Every use in the repo, the C# binding and the docs follow the rename.
- **The public call:** a root takes a navigation command, a direction (up, down, left, right), activate or cancel, from C++ and C#. A direction goes through the same navigation as an arrow key; activate sends the focused element what the gamepad's A sends; cancel sends Cancel. With nothing focused, a direction gives the root its first focus as a gamepad direction does.
- **Glossary:** *Gamepad navigation* becomes *Directional navigation*, covering the arrow keys and a direction the game sends, with *gamepad navigation* as its old name to avoid; *Focusable* says a direction; FenrirUI's README and ADR-0065's wording follow it, all in the rename ticket.
- **No ADR:** it extends ADR-0065's navigation to the keyboard and reverses nothing.

## Testing Decisions

- **A good test here sends input and checks where the focus is and what events were sent**, never how routing is coded.
- **FenrirUI's focus and gamepad tests** (prior art: the focus, gamepad and input tests): each arrow moves focus to the nearest element that way; a focused slider, text field and scroll view keep their arrows; a repeated key moves again; Escape sends Cancel where the element does not take it; with nothing focused, arrows and Escape pass to the game; a root with `directionalNavigation` off ignores the arrows and the gamepad.
- **Tests of the public call:** a sent direction moves focus as an arrow does; activate presses the focused button; cancel sends Cancel; a direction with nothing focused gives the first focus.
- **FenrirUIScripting's fixture tests:** a direction, activate and cancel sent from C# reach the root.
- **By hand:** UIDemo's menus driven with the arrow keys, Enter and Escape; the demo kit still leaves arrows to the scenes, so PhysicsDemo's vehicles drive with the arrows as before. The demo apps' smoke runs pass.

## Out of Scope

- Key rebinding, and an action layer (deferred on the map).
- Home, End, Page Up and Page Down moving focus.
- Tab order changes.

## Further Notes

- The facts behind the decisions are in [Do arrow keys move UI focus?](../engine-gaps/issues/30-do-arrow-keys-move-ui-focus.md).
- Build and test only `msvc-x64-debug` locally, and use the short-path junction in a worktree (`README.md`, "Building and testing").
