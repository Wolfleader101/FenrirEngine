# 02: Arrow keys and Escape drive directional navigation

Spec: [Arrow keys move UI focus](../spec.md).

**What to build:** While a root's element has the focus, the arrow keys send the matching directions through the same navigation the d-pad's use: offered to the focused element alone first, so a control that consumes the key keeps it (the slider, scroll view and text field do), else a move to the nearest focusable element, revealed if scrolled away; repeats come from the keyboard's key repeat. Escape sends the Cancel UI event where the focused element does not take it. With no element of the root focused, arrows and Escape pass to the game and give no first focus. A root with `directionalNavigation` off takes neither arrows nor the gamepad's directions.

**Blocked by:** 01
**Touches:** `packages/FenrirUI/include/FenrirUI/`, `packages/FenrirUI/src/`, `packages/FenrirUI/Tests/`, `packages/FenrirUI/api/`, `examples/UIDemo/README.md`

**Status:** ready-for-agent

Stacked on 01.

- [ ] FenrirUI tests: each arrow moves focus to the nearest element that way; a focused slider, text field and scroll view keep their arrows; a repeated key moves again.
- [ ] Escape sends Cancel where the element does not take it; with nothing focused, arrows and Escape pass to the game.
- [ ] A root with the flag off ignores arrows and the gamepad.
- [ ] UIDemo's menus work with the arrows, Enter and Escape, and PhysicsDemo's vehicles still drive with the arrows (hand checks, noted in UIDemo's README).
